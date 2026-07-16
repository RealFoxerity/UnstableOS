#ifndef FS_VFAT_FAT_INTERNAL_H
#define FS_VFAT_FAT_INTERNAL_H

#include "kernel.h"
#define dkprintf(fmt, ...) kprintf("fat: " fmt, ##__VA_ARGS__)


#define FAT_DIR_FREE 0xE5
#define FAT_DIR_END  0x00

// the low ends
#define FAT_CLUSTER_END_FAT12 0xFF0
#define FAT_CLUSTER_END_FAT16 0xFFF0
#define FAT_CLUSTER_END_FAT32 0x0FFFFFF0

#include "fat_structs.h"

enum fat_type {
    FAT12,
    FAT16,
    FAT32,
};

#include "kernel_spinlock.h"
#include <stddef.h>

// assuming reasonably formatted volumes, 32-bit numbers here should be plenty enough
struct fat_info {
    rw_spinlock_t fs_lock;
    enum fat_type type;
    size_t bytes_per_sector;
    size_t sectors_per_cluster;
    size_t data_clusters;
    size_t last_free_cluster; // or last allocated

    size_t root_dir_cluster; // fat32 only
    struct {
        size_t root_dir_entries;
        size_t root_dir_sector;
    } fat12;
    size_t fat_start_sector;
    size_t sectors_per_fat;
    size_t fat_copies;
    size_t data_sector_start;
    size_t max_chain_len; // (1<<32)/spc/bps

};

#include <time.h>
long fat_name_to_short(const char *pathname, char shortname[11]);
void fat_short_to_name(const char shortname[11], char out[13]);

time_t fat_time_to_epoch(struct fat_time ft, struct fat_date fd);

#include "fs/fs.h"

int fat_lookup(superblock_t * sb, inode_t * last, const char * pathname, inode_t ** inode_out);

// all below require external locking

size_t fat_next_in_chain(size_t last_cluster, const superblock_t * sb);
int fat_set_chain(size_t last_cluster, size_t next, const superblock_t * sb); // 0 if success
size_t fat_get_free_cluster(const superblock_t * sb); // -1 if io error, 0 if not found
int fat_free_chain(size_t first_freed, superblock_t *sb); // 0 if success, frees all clusters starting at first_freed
int fat_end_chain(size_t last_alloced, superblock_t *sb); // 0 if success, terminates chain at last alloced
off_t fat_lookup_cluster_generic(const char name[11], size_t dir_cluster, superblock_t * sb, struct fat_dir_entry * out);
off_t fat12_lookup(const char name[12], superblock_t * sb, struct fat_dir_entry * out);
off_t fat_get_parent(off_t dentry, superblock_t * sb, struct fat_dir_entry * out);
#endif