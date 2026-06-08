#include "block/partitions.h"
#include "dev_ops.h"
#include "kernel.h"
#include <string.h>
#include "mm/kernel_memory.h"
#include "fs/fs.h"
#include <stdint.h>

#include <errno.h>

// 4 major block devs, 8 minor per major (32 drives total)
// pointer to an array of DRIVE_PART_LIMIT partitions -> 2KiB per drive
// that does unfortunately mean that partition 0 is wasted space
static struct partition * partitions[4][8] = {0};
static file_descriptor_t * root_devs[4][8] = {0};

static rw_spinlock_t partitions_lock = {0};

static struct partition ** part_get_table(dev_t dev) {
    if (MAJOR(dev) < DEV_MAJ_BLOCK0 || MAJOR(dev) > DEV_MAJ_BLOCK3)
        return NULL;
    if (MINOR(dev) % 128 == 0)
        return NULL;
    return &partitions[MAJOR(dev) - DEV_MAJ_BLOCK0][MINOR(dev)/DRIVE_PART_LIMIT];
}

static struct partition * part_get(dev_t dev) {
    if (MAJOR(dev) < DEV_MAJ_BLOCK0 || MAJOR(dev) > DEV_MAJ_BLOCK3)
        return NULL;
    if (MINOR(dev) % 128 == 0)
        return NULL;
    if (partitions[MAJOR(dev) - DEV_MAJ_BLOCK0][MINOR(dev)/DRIVE_PART_LIMIT] == NULL)
        return NULL;
    return &partitions[MAJOR(dev) - DEV_MAJ_BLOCK0][MINOR(dev)/DRIVE_PART_LIMIT][MINOR(dev)%DRIVE_PART_LIMIT];
}

static dev_t part_get_root_dev(dev_t part) {
    dev_t minor = MINOR(part);
    minor -= minor % DRIVE_PART_LIMIT;
    return GET_DEV(MAJOR(part), minor);
}

off_t part_seek(file_descriptor_t *file, off_t off, int whence) {
    kassert(file);
    kassert(S_ISBLK(file->inode->mode));

    rw_spinlock_acquire_read(&partitions_lock);
    struct partition * part = part_get(file->inode->device);
    if (part == NULL) {
        rw_spinlock_release_read(&partitions_lock);
        return -ENODEV;
    }
    off_t ret = generic_seek(file, off, whence, part->size);
    rw_spinlock_release_read(&partitions_lock);

    return ret;
}
ssize_t part_pread(file_descriptor_t *file, void *buf, size_t count, off_t offset) {
    kassert(file);
    kassert(S_ISBLK(file->inode->mode));

    rw_spinlock_acquire_read(&partitions_lock);
    struct partition * part = part_get(file->inode->device);
    if (part == NULL) {
        rw_spinlock_release_read(&partitions_lock);
        return -ENODEV;
    }
    if (offset >= part->size) {
        rw_spinlock_release_read(&partitions_lock);
        return 0;
    }
    if (offset + count >= part->size)
        count = part->size - offset;

    file_descriptor_t * drive_file = root_devs[MAJOR(file->inode->device)][MINOR(file->inode->device)/DRIVE_PART_LIMIT];
    kassert(drive_file);


    ssize_t ret = pread_dev(drive_file, buf, count, offset + part->start);
    rw_spinlock_release_read(&partitions_lock);

    return ret;
}
ssize_t part_pwrite(file_descriptor_t *file, const void *buf, size_t count, off_t offset) {
    kassert(file);
    kassert(S_ISBLK(file->inode->mode));

    rw_spinlock_acquire_read(&partitions_lock);
    struct partition * part = part_get(file->inode->device);
    if (part == NULL) {
        rw_spinlock_release_read(&partitions_lock);
        return -ENODEV;
    }
    if (offset >= part->size) {
        rw_spinlock_release_read(&partitions_lock);
        return 0;
    }
    if (offset + count >= part->size)
        count = part->size - offset;

    file_descriptor_t * drive_file = root_devs[MAJOR(file->inode->device)][MINOR(file->inode->device)/DRIVE_PART_LIMIT];
    kassert(drive_file);

    ssize_t ret = pwrite_dev(drive_file, buf, count, offset + part->start);
    rw_spinlock_release_read(&partitions_lock);

    return ret;
}

static const struct dev_operations part_ops = {
    .pread = part_pread,
    .pwrite = part_pwrite,
    .seek = part_seek,
};

long part_del(dev_t old_part) {
    if (MAJOR(old_part) < DEV_MAJ_BLOCK0 || MAJOR(old_part) > DEV_MAJ_BLOCK3)
        return -EINVAL;
    if (MINOR(old_part) % 128 == 0)
        return -EINVAL;
    rw_spinlock_acquire_read(&partitions_lock);
    struct partition ** part_table = part_get_table(old_part);
    if (part_table == NULL || *part_table == NULL ||
        (*part_table)[MINOR(old_part)%DRIVE_PART_LIMIT].size == 0) {
        rw_spinlock_release_read(&partitions_lock);
        return 0;
    }
    rw_spinlock_release_read(&partitions_lock);

    rw_spinlock_acquire_write(&partitions_lock); // not race on add

    if (*part_table == NULL || (*part_table)[MINOR(old_part)%DRIVE_PART_LIMIT].size == 0) { // we raced
        rw_spinlock_release_write(&partitions_lock);
        return 0;
    }

    spinlock_acquire(&kernel_inode_lock); // not race on a brand new inode
    inode_t * part_inode = __get_inode_raw_device(old_part);
    if (part_inode != NULL) {
        spinlock_release(&kernel_inode_lock);
        rw_spinlock_release_write(&partitions_lock);
        return -EBUSY;
    }

    (*part_table)[MINOR(old_part)%DRIVE_PART_LIMIT] = (struct partition){0};

    dev_ops_remove(old_part);

    spinlock_release(&kernel_inode_lock);
    rw_spinlock_release_write(&partitions_lock);

    return 0;
}

long part_add(dev_t new_part, struct partition part) {
    if (MAJOR(new_part) < DEV_MAJ_BLOCK0 || MAJOR(new_part) > DEV_MAJ_BLOCK3)
        return -EINVAL;
    if (MINOR(new_part) % 128 == 0)
        return -EINVAL;
    if (part.size == 0) return -EINVAL;
    if (part.start < 0) return -EINVAL;
    if (part.size < 0) return -EINVAL;

    rw_spinlock_acquire_write(&partitions_lock);
    struct partition ** part_table = part_get_table(new_part);
    if (part_table == NULL) {
        rw_spinlock_release_write(&partitions_lock);
        return -EINVAL;
    }
    // we have to get a file descriptor for reads and writes to the actual medium
    if (root_devs[MAJOR(new_part)][MINOR(new_part)/DRIVE_PART_LIMIT] == NULL) {
        dev_t root_dev = part_get_root_dev(new_part);
        long ret = open_raw_device(root_dev,
            O_RDWR,
            &root_devs[MAJOR(new_part)][MINOR(new_part)/DRIVE_PART_LIMIT]);

        if (ret < 0) {
            rw_spinlock_release_write(&partitions_lock);
            return ret;
        }
        if (root_devs[MAJOR(new_part)][MINOR(new_part)/DRIVE_PART_LIMIT] == NULL) {
            rw_spinlock_release_write(&partitions_lock);
            return -EINVAL; //?
        }
    }

    if (*part_table == NULL) {
        *part_table = kalloc(DRIVE_PART_LIMIT * sizeof(struct partition));
        if (*part_table == NULL) {
            rw_spinlock_release_write(&partitions_lock);
            return -ENOMEM;
        }
        memset(*part_table, 0, DRIVE_PART_LIMIT * sizeof(struct partition));
    }
    if ((*part_table)[MINOR(new_part)%DRIVE_PART_LIMIT].size != 0) {
        rw_spinlock_release_write(&partitions_lock);
        return -EEXIST;
    }

    (*part_table)[MINOR(new_part)%DRIVE_PART_LIMIT] = part;

    dev_register_ops(new_part, &part_ops);
    rw_spinlock_release_write(&partitions_lock);

    dev_t root_dev = part_get_root_dev(new_part);
    char device_name[32];
    if (dev2string(new_part, device_name) == NULL) {
        kprintf("part: dev %hx part %d start %llu size %llu\n",
            root_dev, MINOR(new_part)%DRIVE_PART_LIMIT, part.start, part.size);
    } else {
        kprintf("part: %s start %llu size %llu\n",
            device_name, part.start, part.size);
    }
    return 0;
}