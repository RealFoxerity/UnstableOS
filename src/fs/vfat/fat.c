#include "fs/fs.h"
#include "fs/vfs.h"
#include "fat_structs.h"
#include "fat_internal.h"
#include "kernel.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include "kernel_spinlock.h"

// TODO: support LFN (vfat)

#define dkprintf(fmt, ...) kprintf("fat: " fmt, ##__VA_ARGS__)

int fat_init(superblock_t * sb) {
    struct fat_block block;
    if (pread_file(sb->fd, &block, sizeof(block), 0) < sizeof(block) || block.magic != FAT_MAGIC) {
        dkprintf("Couldn't read the boot block, giving up\n");
        return -EIO;
    }

    if (block.reserved_sectors == 0) {
        dkprintf("Block specifies invalid reserved sector count, giving up\n");
        return -EINVAL;
    }

    switch (block.bytes_per_sector) {
        case 512:
        case 1024:
        case 2048:
        case 4096:
            break;
        default:
            dkprintf("Block specifies invalid sector size, giving up\n");
            return -EINVAL;
    }

    size_t sectors_per_fat = block.sectors_per_fat;
    if (sectors_per_fat == 0)
        sectors_per_fat = block.fat32.sectors_per_fat;
    if (sectors_per_fat == 0) {
        dkprintf("Block specifies 0 sectors per FAT, giving up\n");
        return -EINVAL;
    }


    size_t bytes_per_cluster = block.bytes_per_sector * block.sectors_per_cluster;
    if (bytes_per_cluster == 0) {
        dkprintf("Block specifies invalid cluster size, giving up\n");
        return -EINVAL;
    }

    size_t total_sectors = block.total_sectors;
    if (total_sectors == 0)
        total_sectors = block.total_sectors_32;
    if (total_sectors == 0) {
        dkprintf("Block specifies 0 total sectors, giving up\n");
        return -EINVAL;
    }

    if (seek_file(sb->fd, 0, SEEK_END) < total_sectors * block.bytes_per_sector) {
        dkprintf("Filesystem larger than containing device, giving up\n");
        return -EINVAL;
    }

    size_t first_data_sector = block.reserved_sectors + block.number_of_fats * sectors_per_fat;

    size_t root_directory_bytes = block.root_dir_entries * sizeof(struct fat_dir_entry);
    size_t root_directory_sectors = root_directory_bytes + block.bytes_per_sector - 1;
    root_directory_sectors /= block.bytes_per_sector;

    size_t data_sectors = total_sectors -
        first_data_sector -
        root_directory_sectors;

    size_t data_clusters = data_sectors / block.sectors_per_cluster;

    enum fat_type fat_type = 0;

    // this makes perfect sense, thanks Microsoft :3
    size_t fat_entries = 0;
    if (data_clusters < 4085) {
        fat_entries = sectors_per_fat * block.bytes_per_sector * 2 / 3;
        fat_type = FAT12;
    } else if (data_clusters < 65525) {
        fat_entries = sectors_per_fat * block.bytes_per_sector / 2;
        fat_type = FAT16;
    } else {
        fat_entries = sectors_per_fat * block.bytes_per_sector / 4;
        fat_type = FAT32;
    }

    if (fat_entries < data_clusters) {
        dkprintf("FAT not big enough for specified data clusters, giving up\n");
        return -EINVAL;
    }

    size_t cluster_limit = fat_type == FAT12 ?
        FAT_CLUSTER_END_FAT12 :
        fat_type == FAT16 ?
            FAT_CLUSTER_END_FAT16 :
            FAT_CLUSTER_END_FAT32;

    if (cluster_limit <= data_clusters)
        data_clusters = cluster_limit - 1;
    size_t root_dir_cluster = 2; // first cluster is 0 because 1 and 2 are reserved
    if (fat_type == FAT32)
        root_dir_cluster = block.fat32.root_dir_cluster;

    if (root_dir_cluster < 2 || root_dir_cluster - 2 >= data_clusters) {
        dkprintf("Invalid root directory cluster, giving up\n");
        return -EINVAL;
    }

    sb->data = kalloc(sizeof(struct fat_info));
    if (!sb->data) {
        dkprintf("Not enough memory to allocate internal structures, giving up\n");
        return -ENOMEM;
    }

    size_t root_dir_sector = first_data_sector;
    first_data_sector += root_directory_sectors;

    *(struct fat_info*)sb->data = (struct fat_info) {
        .type = fat_type,
        .bytes_per_sector = block.bytes_per_sector,
        .sectors_per_cluster = block.sectors_per_cluster,
        .data_clusters = data_clusters,
        .root_dir_cluster  = root_dir_cluster,
        .fat12.root_dir_entries = block.root_dir_entries,
        .fat12.root_dir_sector = root_dir_sector,
        .fat_start_sector = block.reserved_sectors,
        .sectors_per_fat = sectors_per_fat,
        .fat_copies = block.number_of_fats,
        .data_sector_start = first_data_sector,
        .max_chain_len = ((unsigned long long)1<<32) / block.sectors_per_cluster / block.bytes_per_sector
    };

    if (fat_type == FAT32) {
        dkprintf("Mounting volume %.11s OEM %.8s, type %.8s, size %llu\n",
            block.fat32.drive_info.volume_label,
            block.oem_name,
            block.fat32.drive_info.file_system_type,
            (unsigned long long)data_sectors*block.bytes_per_sector);
    } else {
        dkprintf("Mounting volume %.11s OEM %.8s, type %.8s, size %llu\n",
            block.fat12.volume_label,
            block.oem_name,
            block.fat12.file_system_type,
            (unsigned long long)data_sectors*block.bytes_per_sector);
    }
    return 0;
}

int fat_deinit(superblock_t *sb) {
    kassert(sb);
    kfree(sb->data);
    return 0;
}

int fat_stat(inode_t * file, struct stat * buf) {
    kassert(file);
    if (!buf)
        return -EFAULT;

    kassert(file->backing_superblock->data);
    struct fat_info * fat_info = file->backing_superblock->data;

    *buf = (struct stat) {
        .st_dev = file->backing_superblock->device,
        .st_ino = file->id,
        .st_mode = file->mode,
        .st_nlink =  1,
        .st_blksize = fat_info->bytes_per_sector * fat_info->sectors_per_cluster
    };
    if (file->id != 0) {
        struct fat_dir_entry dentry_buf = {0};
        // operations like these don't require fs locking
        if (pread_file(file->backing_superblock->fd,
            &dentry_buf, sizeof(dentry_buf),
            file->id) != sizeof(dentry_buf)
        )
            return -EIO;
        buf->st_size = dentry_buf.size;

        buf->st_ctime = fat_time_to_epoch(dentry_buf.ctime, dentry_buf.cdate);
        buf->st_ctime += dentry_buf.ctime_10ms/100;

        buf->st_mtime = fat_time_to_epoch(dentry_buf.mtime, dentry_buf.mdate);

        buf->st_atime = fat_time_to_epoch((struct fat_time){0}, dentry_buf.adate);
    }
    return 0;
}

ssize_t fat_readdir(file_descriptor_t * fd, struct dirent * dent, size_t dent_size, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->backing_superblock);
    kassert(fd->inode->backing_superblock->data);

    if (!dent)
        return -EFAULT;
    // it would be better to check for exact files
    // however userspace passing this small of a struct is bs and we don't yet support LFNs
    // 13 for short name + . + \0
    if (dent_size < sizeof(struct dirent) + 13)
        return -EINVAL;

    struct fat_info * fi = fd->inode->backing_superblock->data;
    superblock_t * sb = fd->inode->backing_superblock;

    struct fat_dir_entry dentry_buf = {0};

    switch (offset) {
        case 0: // .
            *dent = (struct dirent) {
                .d_ino = fd->inode->id,
                .d_off = offset,
                .d_reclen = sizeof(struct dirent) + 2,
                .d_type = DT_DIR,
            };
            dent->d_name[0] = '.';
            dent->d_name[1] = '\0';
            break;
        case 1:
            *dent = (struct dirent) {
                .d_ino = 0,
                .d_off = offset,
                .d_reclen = sizeof(struct dirent) + 3,
                .d_type = DT_DIR,
            };
            if (fd->inode->id) {
                // fat_get_parent scans the directory which could change
                // so spinlocks; in the if() because otherwise it's pointless to lock
                rw_spinlock_acquire_read(&fi->fs_lock);
                dent->d_ino = fat_get_parent(fd->inode->id, sb, NULL);
                rw_spinlock_release_read(&fi->fs_lock);
            }
            memcpy(dent->d_name, "..", 3);
            break;
        default:
            // have to walk each entry in cases of free \xE5 entries or early end
            if (!fd->inode->id && fi->type != FAT32) {
                if (offset >= fi->fat12.root_dir_entries)
                    return 0;
                off_t current_offset = 2; // root doesn't have "." and ".."
                rw_spinlock_acquire_read(&fi->fs_lock);
                for (int i = 0; i < fi->fat12.root_dir_entries; i++) {
                    off_t dir_off = fi->fat12.root_dir_sector * fi->bytes_per_sector + i * sizeof(struct fat_dir_entry);
                    if (pread_file(sb->fd,
                            &dentry_buf, sizeof(dentry_buf),
                            dir_off
                        ) != sizeof(dentry_buf)
                    ) {
                        rw_spinlock_release_read(&fi->fs_lock);
                        return -EIO;
                    }
                    if ((unsigned char)dentry_buf.name[0] == FAT_DIR_FREE) continue;
                    if (dentry_buf.name[0] == FAT_DIR_END) goto notfound;
                    if (dentry_buf.attr & FAT_DENTRY_ATTR_VOLLBL) continue;
                    if (current_offset == offset) {
                        rw_spinlock_release_read(&fi->fs_lock);
                        *dent = (struct dirent) {
                            .d_ino = dir_off,
                            .d_off = offset,
                            .d_type = dentry_buf.attr & FAT_DENTRY_ATTR_SUBDIR ? DT_DIR : DT_REG,
                        };
                        char name[13]; // prevent userspace fucking it up
                        fat_short_to_name(dentry_buf.name, name);
                        dent->d_reclen = sizeof(struct dirent) + strlen(name) + 1;
                        strcpy(dent->d_name, name);
                        goto end;
                    }
                    current_offset++;
                }
            } else {
                size_t dir_cluster;
                if (!fd->inode->id)
                    dir_cluster = fi->root_dir_cluster;
                else {
                    if (pread_file(sb->fd,
                            &dentry_buf, sizeof(dentry_buf),
                            fd->inode->id) != sizeof(dentry_buf)
                    ) {
                        return -EIO;
                    }
                    dir_cluster = dentry_buf.start_cluster;
                    if (fi->type == FAT32)
                        dir_cluster |= dentry_buf.fat32_cluster_hi << 16;
                }

                if (dir_cluster < 2)
                    return -EIO;

                size_t cluster_limit = fi->type == FAT12 ?
                    FAT_CLUSTER_END_FAT12 :
                    fi->type == FAT16 ?
                        FAT_CLUSTER_END_FAT16 :
                        FAT_CLUSTER_END_FAT32;

                off_t current_offset = fd->inode->id ? 0 : 2; // root doesn't have "." and ".."
                size_t visited_clusters = 0;
                rw_spinlock_acquire_read(&fi->fs_lock);
                while (dir_cluster < cluster_limit && visited_clusters < fi->max_chain_len) {
                    for (size_t i = 0; i < fi->bytes_per_sector * fi->sectors_per_cluster / sizeof(struct fat_dir_entry); i++) {
                        off_t dir_off = fi->data_sector_start + (dir_cluster - 2) * fi->sectors_per_cluster;
                        dir_off *= fi->bytes_per_sector;
                        dir_off += i * sizeof(struct fat_dir_entry);
                        if (pread_file(sb->fd,
                                &dentry_buf, sizeof(dentry_buf),
                                dir_off
                            ) != sizeof(dentry_buf)
                        ) {
                            rw_spinlock_release_read(&fi->fs_lock);
                            return -EIO;
                        }
                        if ((unsigned char)dentry_buf.name[0] == FAT_DIR_FREE) continue;
                        if (dentry_buf.name[0] == FAT_DIR_END) goto notfound;
                        if (dentry_buf.attr & FAT_DENTRY_ATTR_VOLLBL) continue;
                        if (current_offset == offset) {
                            rw_spinlock_release_read(&fi->fs_lock);
                            *dent = (struct dirent) {
                                .d_ino = dir_off,
                                .d_off = offset,
                                .d_type = dentry_buf.attr & FAT_DENTRY_ATTR_SUBDIR ? DT_DIR : DT_REG,
                            };
                            char name[13]; // prevent userspace fucking it up during strlen
                            fat_short_to_name(dentry_buf.name, name);
                            dent->d_reclen = sizeof(struct dirent) + strlen(name) + 1;
                            strcpy(dent->d_name, name);
                            goto end;
                        }
                        current_offset++;
                    }
                    dir_cluster = fat_next_in_chain(dir_cluster, sb);
                    if (dir_cluster < 2 || dir_cluster == -1) {
                        rw_spinlock_release_read(&fi->fs_lock);
                        return -EIO;
                    }
                    visited_clusters ++;
                }
            }
            notfound:
            rw_spinlock_release_read(&fi->fs_lock);
            return 0;
    }

    end:
    offset++;
    rw_spinlock_acquire_write(&fd->access_lock);
    fd->off = offset;
    rw_spinlock_release_write(&fd->access_lock);
    return dent->d_reclen;
}

off_t fat_seek(file_descriptor_t * fd, off_t off, int whence) {
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->backing_superblock);

    struct fat_dir_entry dentry_buf = {0};
    if (pread_file(fd->inode->backing_superblock->fd,
        &dentry_buf, sizeof(dentry_buf),
        fd->inode->id) != sizeof(dentry_buf)
    )
        return -EIO;

    return generic_seek(fd, off, whence, dentry_buf.size);
}

ssize_t fat_pread(file_descriptor_t * fd, void * buf, size_t n, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->backing_superblock);
    superblock_t * sb = fd->inode->backing_superblock;
    kassert(sb->data);

    if (!buf)
        return -EFAULT;

    if (offset < 0) return -EINVAL;
    if (!S_ISREG(fd->inode->mode)) return -EINVAL;

    if (n == 0) return 0;
#ifdef E2BIG_ON_2G
    if (n > SSIZE_MAX) return -E2BIG;
#else
    if (n > SSIZE_MAX) n = SSIZE_MAX;
#endif

    if (offset >= (off_t)1<<32)
        return EOVERFLOW;

    struct fat_info * fi = sb->data;
    ssize_t read_bytes = 0;

    rw_spinlock_acquire_read(&fi->fs_lock);

    // this has to be locked to prevent meddling with the size field
    struct fat_dir_entry dentry_buf = {0};
    if (pread_file(fd->inode->backing_superblock->fd,
        &dentry_buf, sizeof(dentry_buf),
        fd->inode->id) != sizeof(dentry_buf)
    ) {
        rw_spinlock_release_read(&fi->fs_lock);
        return -EIO;
    }

    size_t cluster_limit = fi->type == FAT12 ?
        FAT_CLUSTER_END_FAT12 :
        fi->type == FAT16 ?
            FAT_CLUSTER_END_FAT16 :
            FAT_CLUSTER_END_FAT32;

    if (offset >= dentry_buf.size) {
        rw_spinlock_release_read(&fi->fs_lock);
        return 0;
    }

    if (offset + n >= dentry_buf.size)
        n = dentry_buf.size - offset;

    size_t bytes_per_cluster =  fi->sectors_per_cluster * fi->bytes_per_sector;
    size_t skipped_clusters = offset / bytes_per_cluster;
    offset %= bytes_per_cluster;

    size_t start_cl = dentry_buf.start_cluster;
    if (fi->type == FAT32)
        start_cl |= dentry_buf.fat32_cluster_hi << 16;

    if (start_cl < 2 || start_cl >= cluster_limit) {
        rw_spinlock_release_read(&fi->fs_lock);
        return -EIO;
    }

    for (int i = 0; i < skipped_clusters; i++) {
        start_cl = fat_next_in_chain(start_cl, sb);
        if (start_cl == -1 || start_cl < 2 || start_cl >= cluster_limit) {
            rw_spinlock_release_read(&fi->fs_lock);
            return -EIO;
        }
    }

    // start partial read
    size_t to_read = n;
    if (offset + n >= bytes_per_cluster)
        to_read = bytes_per_cluster - offset;

    read_bytes = pread_file(sb->fd,
        buf, to_read,
        (fi->data_sector_start + (start_cl - 2) * fi->sectors_per_cluster) * fi->bytes_per_sector + offset);
    if (read_bytes != bytes_per_cluster - offset)
        goto end;
    n -= to_read;
    offset = 0;
    if (n == 0)
        goto end;

    while (n > 0) {
        start_cl = fat_next_in_chain(start_cl, sb);
        if (start_cl == -1 || start_cl < 2) {
            rw_spinlock_release_read(&fi->fs_lock);
            return -EIO;
        }
        if (start_cl >= cluster_limit)
            goto end;


        to_read = n;
        if (n > bytes_per_cluster)
            to_read = bytes_per_cluster;

        ssize_t ret = pread_file(sb->fd,
            buf + read_bytes, to_read,
            (fi->data_sector_start + (start_cl - 2) * fi->sectors_per_cluster) * fi->bytes_per_sector + offset);

        if (ret < 0) {
            rw_spinlock_release_read(&fi->fs_lock);
            return ret;
        }
        read_bytes += ret;
        if (ret != to_read) // shouldn't happen
            goto end;

        n -= to_read;
    }

    end:
    rw_spinlock_release_read(&fi->fs_lock);
    return read_bytes;
}

struct vfs_ops fat_ops = {
    .fs_init   = fat_init,
    .fs_deinit = fat_deinit,
    .lookup    = fat_lookup,
    .stat      = fat_stat,
    .readdir   = fat_readdir,
    .seek      = fat_seek,
    .pread     = fat_pread,
};