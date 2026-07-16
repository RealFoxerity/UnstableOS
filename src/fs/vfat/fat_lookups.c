#include "fat_internal.h"
#include "fat_structs.h"
#include <sys/types.h>
#include "fs/fs.h"
#include "fs/vfs.h"
#include <errno.h>
#include <string.h>

// I am so sorry for everyone reading this code
// especially the get_parent stuff,
// I really couldn't find a better solution other than allocating partial in-memory fs structures

// doing visited_clusters in case there's a loop in the cluster chain
// calculated from the amount of clusters to get a 4GB file (the max)

off_t fat_lookup_cluster_generic(const char name[11], size_t dir_cluster, superblock_t * sb, struct fat_dir_entry * out) {
    struct fat_info * fat_info = sb->data;

    struct fat_dir_entry dentry_buf = {0};
    size_t cluster_limit = fat_info->type == FAT12 ?
        FAT_CLUSTER_END_FAT12 :
        fat_info->type == FAT16 ?
            FAT_CLUSTER_END_FAT16 :
            FAT_CLUSTER_END_FAT32;

    size_t visited_clusters = 0;
    while (dir_cluster < cluster_limit && visited_clusters < fat_info->max_chain_len) {
        for (size_t i = 0; i < fat_info->bytes_per_sector * fat_info->sectors_per_cluster / sizeof(struct fat_dir_entry); i++) {
            off_t dir_off = fat_info->data_sector_start + (dir_cluster - 2) * fat_info->sectors_per_cluster;
            dir_off *= fat_info->bytes_per_sector;
            dir_off += i * sizeof(struct fat_dir_entry);
            if (pread_file(sb->fd,
                    &dentry_buf, sizeof(dentry_buf),
                    dir_off
                ) != sizeof(dentry_buf))
                return -EIO;
            if ((unsigned char)dentry_buf.name[0] == FAT_DIR_FREE) continue;
            if (dentry_buf.name[0] == FAT_DIR_END) return -ENOENT;
            if (dentry_buf.attr & FAT_DENTRY_ATTR_VOLLBL) continue;
            if (memcmp(dentry_buf.name, name, 11) == 0) {
                if (out)
                    *out = dentry_buf;
                return dir_off;
            }
        }
        dir_cluster = fat_next_in_chain(dir_cluster, sb);
        if (dir_cluster < 2 || dir_cluster == -1)
            return -EIO;
        visited_clusters++;
    }
    return -ENOENT;
}

off_t fat12_lookup(const char name[11], superblock_t * sb, struct fat_dir_entry * out) {
    struct fat_info * fi = sb->data;

    struct fat_dir_entry dentry_buf = {0};

    for (int i = 0; i < fi->fat12.root_dir_entries; i++) {
        off_t dir_off = fi->fat12.root_dir_sector * fi->bytes_per_sector + i * sizeof(struct fat_dir_entry);
        if (pread_file(sb->fd,
                &dentry_buf, sizeof(dentry_buf),
                dir_off
            ) != sizeof(dentry_buf))
            return -EIO;
        if ((unsigned char)dentry_buf.name[0] == FAT_DIR_FREE) continue;
        if (dentry_buf.name[0] == FAT_DIR_END) return -ENOENT;
        if (dentry_buf.attr & FAT_DENTRY_ATTR_VOLLBL) continue;
        if (memcmp(dentry_buf.name, name, 11) == 0) {
            if (out)
                *out = dentry_buf;
            return dir_off;
        }
    }
    return -ENOENT;
}

off_t fat_get_parent(off_t dentry, superblock_t * sb, struct fat_dir_entry * out) {
    if (dentry == 0) return 0;

    struct fat_info * fi = sb->data;
    struct fat_dir_entry dentry_buf = {0};

    if (pread_file(sb->fd, &dentry_buf, sizeof(dentry_buf), dentry) != sizeof(dentry_buf))
        return -EIO;

    size_t cluster = dentry_buf.start_cluster;
    if (fi->type == FAT32)
        cluster |= dentry_buf.fat32_cluster_hi;

    // can't just round down in case we're in the 2nd+ cluster of our directory
    off_t ret = fat_lookup_cluster_generic("..         ", cluster, sb, &dentry_buf);
    if (ret < 0)
        return ret;

    // proper parent's cluster
    cluster = dentry_buf.start_cluster;
    if (fi->type == FAT32)
        cluster |= dentry_buf.fat32_cluster_hi;

    if (cluster == 0)
        return 0; // /a/..

    size_t parent_cluster = cluster;

    // grandparent
    ret = fat_lookup_cluster_generic("..         ", cluster, sb, &dentry_buf);
    if (ret < 0)
        return ret;

    cluster = dentry_buf.start_cluster;
    if (fi->type == FAT32)
        cluster |= dentry_buf.fat32_cluster_hi;

    if (cluster == 0 && fi->type == FAT32)
        cluster = fi->root_dir_cluster;

    if (cluster == 0) { // grandparent is root on a FAT12/16 system
        for (int i = 0; i < fi->fat12.root_dir_entries; i++) {
            off_t dir_off = fi->fat12.root_dir_sector * fi->bytes_per_sector + i * sizeof(struct fat_dir_entry);
            if (pread_file(sb->fd,
                    &dentry_buf, sizeof(dentry_buf),
                    dir_off
                ) != sizeof(dentry_buf))
                return -EIO;
            if ((unsigned char)dentry_buf.name[0] == FAT_DIR_FREE) continue;
            if (dentry_buf.name[0] == FAT_DIR_END) return -ENOENT;
            if (dentry_buf.attr & FAT_DENTRY_ATTR_VOLLBL) continue;
            if (dentry_buf.start_cluster == parent_cluster) {
                if (out)
                    *out = dentry_buf;
                return dir_off;
            }
        }
        return -ENOENT;
    }
    size_t cluster_limit = fi->type == FAT12 ?
        FAT_CLUSTER_END_FAT12 :
        fi->type == FAT16 ?
            FAT_CLUSTER_END_FAT16 :
            FAT_CLUSTER_END_FAT32;

    size_t last_cluster = dentry_buf.start_cluster;
    if (sb->fs_type == FAT32)
        last_cluster |= dentry_buf.fat32_cluster_hi << 16;
    size_t visited_clusters = 0;
    while (last_cluster < cluster_limit && visited_clusters < fi->max_chain_len) {
        for (size_t i = 0; i < fi->bytes_per_sector * fi->sectors_per_cluster / sizeof(struct fat_dir_entry); i++) {
            off_t dir_off = fi->data_sector_start + (last_cluster - 2) * fi->sectors_per_cluster;
            dir_off *= fi->bytes_per_sector;
            dir_off += i * sizeof(struct fat_dir_entry);
            if (pread_file(sb->fd,
                    &dentry_buf, sizeof(dentry_buf),
                    dir_off
                ) != sizeof(dentry_buf))
                return -EIO;
            if ((unsigned char)dentry_buf.name[0] == FAT_DIR_FREE) continue;
            if (dentry_buf.name[0] == FAT_DIR_END) return -ENOENT;
            if (dentry_buf.attr & FAT_DENTRY_ATTR_VOLLBL) continue;

            cluster = dentry_buf.start_cluster;
            if (sb->fs_type == FAT32)
                cluster |= dentry_buf.fat32_cluster_hi << 16;

            if (cluster == parent_cluster) {
                if (out)
                    *out = dentry_buf;
                return dir_off;
            }
        }
        last_cluster = fat_next_in_chain(last_cluster, sb);
        if (last_cluster < 2 || last_cluster == -1)
            return -EIO;
        visited_clusters++;
    }
    return -ENOENT;
}

int fat_lookup(superblock_t * sb, inode_t * last, const char * pathname, inode_t ** inode_out) {
    if (!pathname) return -EFAULT;
    if (!sb) return -EFAULT;
    if (!inode_out) return -EFAULT;

    kassert(sb->data);
    struct fat_info * fat_info = sb->data;

    struct fat_dir_entry dentry_buf = {0};
    off_t ret = 0;

    if (last != NULL && !S_ISDIR(last->mode))
        return -ENOTDIR;

    if (strcmp(pathname, ".") == 0) {
        if (!last) {
            // root dir lookup
            // inode number is usually a byte offset to the directory entry
            // in this case 0 as that's normally an invalid value
            inode_t root_dir;
            create_root:
            root_dir = (inode_t){
                .backing_superblock = sb,
                .id = 0,
                .mode = 0777 | S_IFDIR,
            };
            return register_inode(&root_dir, inode_out);
        }

        // FAT directories do have a . entry, but this is faster
        if (!S_ISDIR(last->mode))
            return -ENOTDIR;
        *inode_out = last;
        __atomic_add_fetch(&last->instances, 1, __ATOMIC_ACQUIRE);
        return 0;
    }

    if (strcmp(pathname, "..") == 0) {
        if (!last || last->id == 0)
            return VFS_LOOKUP_ESCAPE;

        if (!S_ISDIR(last->mode))
            return -ENOTDIR;

        // we need to properly resolve .. to not have double ids
        rw_spinlock_acquire_read(&fat_info->fs_lock);
        ret = fat_get_parent(last->id, sb, &dentry_buf);
        rw_spinlock_release_read(&fat_info->fs_lock);
        if (ret < 0)
            return (int)ret;
        if (ret == 0)
            goto create_root;
        goto create_file;
    }
    char name[12] = {0};

    long check = 0;
    if ((check = fat_name_to_short(pathname, name)))
        return check;

    off_t last_entry = last ? last->id : 0;

    rw_spinlock_acquire_read(&fat_info->fs_lock);
    // fat12/16 don't use the clustering system for the root directory
    if (!last_entry && fat_info->type != FAT32) {
        off_t dir_off = fat12_lookup(name, sb, &dentry_buf);
        rw_spinlock_release_read(&fat_info->fs_lock);

        if (dir_off < 0)
            return (int)dir_off;

        inode_t file = {
            .backing_superblock = sb,
            .id   = dir_off,
            .size = dentry_buf.size,
            .mode = 0777 | (dentry_buf.attr & FAT_DENTRY_ATTR_SUBDIR ? S_IFDIR : S_IFREG)
        };
        return register_inode(&file, inode_out);
    }

    if (!last_entry && fat_info->type == FAT32)
        last_entry = fat_info->root_dir_cluster;
    else {
        if (pread_file(sb->fd, &dentry_buf, sizeof(dentry_buf), last_entry) != sizeof(dentry_buf)) {
            rw_spinlock_release_read(&fat_info->fs_lock);
            return -EIO;
        }
        last_entry = dentry_buf.start_cluster;
        if (fat_info->type == FAT32)
            last_entry |= dentry_buf.fat32_cluster_hi << 16;
    }

    ret = fat_lookup_cluster_generic(name, last_entry, sb, &dentry_buf);

    rw_spinlock_release_read(&fat_info->fs_lock);
    create_file:
    if (ret < 0)
        return (int)ret;
    inode_t file = {
        .backing_superblock = sb,
        .id   = ret,
        .size = dentry_buf.size,
        .mode = 0777 | (dentry_buf.attr & FAT_DENTRY_ATTR_SUBDIR ? S_IFDIR : S_IFREG)
    };
    return register_inode(&file, inode_out);
}