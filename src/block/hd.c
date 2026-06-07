// Abstraction above the ATA driver to handle sector caching and VFS operations
// note: this only handles the devices themselves, not partitions, those have to be handled separately
#include "fs/fs.h"
#include "kernel.h"
#include "block/ata/ata.h"
#include <UnstableOS/devs.h>
#include "dev_ops.h"
#include <errno.h>

#include <string.h>

#define ATA_BUSID(dev)   (MAJOR((dev)) - DEV_MAJ_BLOCK0)
#define ATA_DRIVEID(dev) (MINOR((dev)) / DRIVE_PART_LIMIT)
static struct ata_drive * hd_get_ata_drive(dev_t dev) {
    if (MAJOR(dev) > DEV_MAJ_BLOCK3 || MAJOR(dev) < DEV_MAJ_BLOCK0) // not a drive
        return NULL;
    if (MINOR(dev) % DRIVE_PART_LIMIT != 0) // is a partition
        return NULL;

    unsigned int bus_id       = ATA_BUSID(dev);
    unsigned int drive_number = ATA_DRIVEID(dev);

    if (bus_id >= 2 || drive_number >= 2)
        return NULL; // TODO: change when adding more drive controllers

    if (!ata_buses[bus_id].is_initialized)               return NULL;
    if (!ata_buses[bus_id].drives[drive_number].present) return NULL;

    return &ata_buses[bus_id].drives[drive_number];
}

static rw_spinlock_t hd_cache_lock = {0};

struct hd_sector_cache {
    dev_t dev;
    char is_dirty;
    rw_spinlock_t dirty_lock; // acquire "read" to set to 1, "write" to 0 (in sync)
    uint64_t lba;
    void * data;
    struct hd_sector_cache *next;
};

// this basically results in 256*5*512 = 655K of cache, assuming 512 byte sectors
#define HD_SECTOR_BUCKETS 256
#define HD_SECTOR_LL_DEPTH 5 // length of a linked list before removing the oldest element

struct hd_sector_cache hd_sector_cache[HD_SECTOR_BUCKETS];

// the hashtable implementation from devs.c
static unsigned int hd_get_key(dev_t dev, uint64_t lba) {
    uint64_t a = dev * 1103515245;
    uint64_t b = lba * 5838519855;

    uint64_t c = a ^ b;
    return c % HD_SECTOR_BUCKETS;
}

// assumed to have read lock
static char hd_cache_flush_entry(struct hd_sector_cache * entry) {
    rw_spinlock_acquire_write(&entry->dirty_lock);
    if (!entry->is_dirty) {
        rw_spinlock_release_write(&entry->dirty_lock);
        return 1;
    }
    char ret = ata_write(ATA_BUSID(entry->dev), ATA_DRIVEID(entry->dev),
        entry->lba,
        entry->data
    );
    entry->is_dirty = 0;
    rw_spinlock_release_write(&entry->dirty_lock);
    return ret;
}
void hd_cache_flush() {
    rw_spinlock_acquire_read(&hd_cache_lock);
    for (int i = 0; i < HD_SECTOR_BUCKETS; i++) {
        for (struct hd_sector_cache * entry = &hd_sector_cache[i]; entry != NULL; entry = entry->next) {
            hd_cache_flush_entry(entry);
        }
    }
    rw_spinlock_release_read(&hd_cache_lock);
}

static void hd_cache_set(dev_t dev, uint64_t lba, void * data, char dirty) {
    kassert(data);
    struct hd_sector_cache * hash_entry = &hd_sector_cache[hd_get_key(dev, lba)];

    rw_spinlock_acquire_write(&hd_cache_lock);
    if (hash_entry->data == NULL) {
        hash_entry->dev  = dev;
        hash_entry->lba  = lba;
        hash_entry->data = data;
        hash_entry->is_dirty = dirty;
        hash_entry->dirty_lock = (rw_spinlock_t){0}; // to be safe
        rw_spinlock_release_write(&hd_cache_lock);
        return;
    }

    struct hd_sector_cache * last = NULL;
    unsigned int ll_len = 0;
    for (; hash_entry != NULL; last = hash_entry, hash_entry = hash_entry->next, ll_len ++) {
        if (hash_entry->dev == dev && hash_entry->lba == lba) { // found the entry, current behavior is to overwrite
            // this assumes nothing can hold the dirty lock if we hold write lock for cache
            if (hash_entry->is_dirty)
                hd_cache_flush_entry(hash_entry);

            kfree(hash_entry->data);

            hash_entry->data = data;
            hash_entry->is_dirty = dirty;
            hash_entry->dirty_lock = (rw_spinlock_t){0}; // to be safe
            rw_spinlock_release_write(&hd_cache_lock);
            return;
        }
    }
    last->next = kalloc(sizeof(struct hd_sector_cache));
    kassert(last->next);
    last->next->dev  = dev;
    last->next->lba  = lba;
    last->next->data = data;
    last->next->is_dirty = dirty;
    last->next->dirty_lock = (rw_spinlock_t){0};
    last->next->next = NULL;
    if (ll_len > HD_SECTOR_LL_DEPTH) {
        //kprintf("Cache layer: removing cache entry for dev %hx lba %llu\n", dev, lba);
        if (hd_sector_cache[hd_get_key(dev, lba)].is_dirty)
            hd_cache_flush_entry(&hd_sector_cache[hd_get_key(dev, lba)]);

        kfree(hd_sector_cache[hd_get_key(dev, lba)].data);

        memcpy(&hd_sector_cache[hd_get_key(dev, lba)],
            hd_sector_cache[hd_get_key(dev, lba)].next,
            sizeof(struct hd_sector_cache));
    }
    rw_spinlock_release_write(&hd_cache_lock);
}

static struct hd_sector_cache * hd_cache_get(dev_t dev, uint64_t lba) {
    // have to lock outside of this function to not potentially race on overwriting/freeing this cache entry
    //rw_spinlock_acquire_read(&hd_cache_lock);
    for (
            struct hd_sector_cache * hash_entry = &hd_sector_cache[hd_get_key(dev, lba)];
            hash_entry != NULL;
            hash_entry = hash_entry->next
        ) {
        if (hash_entry->dev == dev && hash_entry->lba == lba)
            //rw_spinlock_release_read(&hd_cache_lock);
            return hash_entry;
    }
    //rw_spinlock_release_read(&hd_cache_lock);
    return NULL;
}

static long hd_read_and_cache_ata(const struct ata_drive * drive, dev_t device, uint64_t lba) {
    //kprintf("Cache layer: Fetching new block lba %llu\n", lba);
    void * block = kalloc(drive->sector_size);
    if (!block) {
        kprintf("Out of memory on allocating for block cache!\n");
        return -ENOMEM;
    }

    char ret = ata_read(
        ATA_BUSID(device), ATA_DRIVEID(device),
        lba,
        block
    );
    if (ret <= 0) {
        kfree(block);
        return -EIO;
    }
    hd_cache_set(device, lba, block, 0);

    return 0;
}

ssize_t hd_read_ata(file_descriptor_t *file, void *buf, size_t count) {
    kassert(file);
    kassert(S_ISBLK(file->inode->mode));

#ifdef E2BIG_ON_2G
    if (count > SSIZE_MAX) return -E2BIG;
#else
    if (count > SSIZE_MAX) count = SSIZE_MAX;
#endif
    if (count == 0) return 0;

    struct ata_drive * drive = hd_get_ata_drive(file->inode->device);
    if (drive == NULL) return -ENODEV;

    size_t read = 0;

    for (uint64_t lba = file->off / drive->sector_size;
        lba < (file->off + count + drive->sector_size - 1) / drive->sector_size &&
            lba <= drive->sector_count;
        lba++
    ) {
        lookup_again:
        rw_spinlock_acquire_read(&hd_cache_lock);
        struct hd_sector_cache * cached = hd_cache_get(file->inode->device, lba);
        if (cached == NULL) {
            rw_spinlock_release_read(&hd_cache_lock);

            long ret = hd_read_and_cache_ata(drive, file->inode->device, lba);
            if (ret < 0) return ret;

            // a little slower, however this makes the code cleaner, and we don't have to fight locking
            goto lookup_again;
        }


        // because of how we do this, this means the first iteration
        // theoretically +read is not required
        if ((file->off + read) % drive->sector_size) {
            if (drive->sector_size - (file->off + read) % drive->sector_size < count - read) {
                memcpy(buf + read, cached->data + (file->off + read) % drive->sector_size,
                    drive->sector_size - (file->off + read) % drive->sector_size);
                read += drive->sector_size - (file->off + read) % drive->sector_size;
            } else { // also means the last iteration
                memcpy(buf + read, cached->data + (file->off + read) % drive->sector_size,
                    count - read);
                read += count;
            }
        } else { // block aligned reads
            if (drive->sector_size > count - read) { // last iteration
                memcpy(buf + read, cached->data, count - read);
                read += count - read;
            } else {
                memcpy(buf + read, cached->data, drive->sector_size);
                read += drive->sector_size;
            }
        }

        rw_spinlock_release_read(&hd_cache_lock);
    }

    file->off += read;

    return (ssize_t)read;
}

ssize_t hd_write_ata(file_descriptor_t *file, const void *buf, size_t count) {
    kassert(file);
    kassert(S_ISBLK(file->inode->mode));

#ifdef E2BIG_ON_2G
    if (count > SSIZE_MAX) return -E2BIG;
#else
    if (count > SSIZE_MAX) count = SSIZE_MAX;
#endif
    if (count == 0) return 0;

    struct ata_drive * drive = hd_get_ata_drive(file->inode->device);
    if (drive == NULL) return -ENODEV;

    size_t written = 0;

    for (uint64_t lba = file->off / drive->sector_size;
        lba < (file->off + count + drive->sector_size - 1) / drive->sector_size &&
            lba <= drive->sector_count;
        lba++
    ) {
        lookup_again:
        rw_spinlock_acquire_read(&hd_cache_lock);
        struct hd_sector_cache * cached = hd_cache_get(file->inode->device, lba);
        if (cached == NULL) {
            rw_spinlock_release_read(&hd_cache_lock);

            // aligned write of the entire block
            if ((file->off + written) % drive->sector_size == 0 &&
                count - written >= drive->sector_size
            ) {
                void * block = kalloc(drive->sector_size);
                if (!block) {
                    kprintf("Out of memory on allocating for block cache!\n");
                    return -ENOMEM;
                }
                memcpy(block, buf + written, drive->sector_size);
                if (file->flags & O_SYNC) {
                    char ret = ata_write(ATA_BUSID(file->inode->device), ATA_DRIVEID(file->inode->device),
                        lba,
                        block
                    );
                    if (ret <= 0) {
                        kfree(block);
                        return -EIO;
                    }
                    hd_cache_set(file->inode->device, lba, block, 0);
                } else {
                    hd_cache_set(file->inode->device, lba, block, 1);
                }
                written += drive->sector_size;
                continue;
            }

            long ret = hd_read_and_cache_ata(drive, file->inode->device, lba);
            if (ret < 0) return ret;

            // a little slower, however this makes the code cleaner, and we don't have to fight locking
            goto lookup_again;
        }


        // because of how we do this, this means the first iteration
        // theoretically +read is not required
        if ((file->off + written) % drive->sector_size) {
            if (drive->sector_size - (file->off + written) % drive->sector_size < count - written) {
                memcpy(cached->data + (file->off + written) % drive->sector_size,
                    buf + written,
                    drive->sector_size - (file->off + written) % drive->sector_size);
                written += drive->sector_size - (file->off + written) % drive->sector_size;
            } else { // also means the last iteration
                memcpy(cached->data + (file->off + written) % drive->sector_size,
                    buf + written,
                    count - written);
                written += count;
            }
        } else { // block aligned write
            if (drive->sector_size > count - written) { // last iteration
                memcpy(cached->data, buf + written, count - written);
                written += count - written;
            } else {
                memcpy(cached->data, buf + written, drive->sector_size);
                written += drive->sector_size;
            }
        }

        char ret = 1;

        if (file->flags & O_SYNC) {
            ret = hd_cache_flush_entry(cached);
        } else {
            rw_spinlock_acquire_read(&cached->dirty_lock);
            __atomic_store_n(&cached->is_dirty, 1, __ATOMIC_RELEASE);
            rw_spinlock_release_read(&cached->dirty_lock);
        }

        rw_spinlock_release_read(&hd_cache_lock);

        if (ret <= 0) return -EIO;
    }

    file->off += written;

    return (ssize_t)written;
}

off_t hd_seek_ata(file_descriptor_t *file, off_t off, int whence) {
    kassert(file);
    kassert(S_ISBLK(file->inode->mode));

    struct ata_drive * drive = hd_get_ata_drive(file->inode->device);
    if (drive == NULL) return -ENODEV;

    size_t max_off = drive->sector_count * drive->sector_size;

    switch (whence) {
        case SEEK_SET:
            if (off < 0) return -EINVAL;
            if (off > max_off) return -EINVAL;
            return file->off = off;
        case SEEK_CUR:
            if (file->off + off > file->off && off < 0) return -EINVAL; // underflow - negative offset
            if (file->off + off < file->off && off > 0) return -E2BIG; // overflow

            if (file->off + off > max_off) return -EINVAL;
            return file->off += off;
        case SEEK_END:
            if (off > 0) return -EINVAL;
            if (off == 0) return file->off = max_off;
            if (-off <= file->off) return file->off = file->off - off;
            return -EINVAL; // negative offset
        default:
            return -EINVAL;
    }
}

static const struct dev_operations ata_ops = {
    .read  = hd_read_ata,
    .write = hd_write_ata,
    .seek  = hd_seek_ata,
};

void hd_initialize_drive_devices() {
    // static map the ATA drives
    dev_register_ops(GET_DEV(DEV_MAJ_BLOCK0, DEV_BLOCK_DRIVE0), &ata_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_BLOCK0, DEV_BLOCK_DRIVE1), &ata_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_BLOCK0, DEV_BLOCK_DRIVE2), &ata_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_BLOCK0, DEV_BLOCK_DRIVE3), &ata_ops);
}