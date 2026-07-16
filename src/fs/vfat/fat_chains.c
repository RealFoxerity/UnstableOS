#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include "kernel.h"
#include "fs/fs.h"
#include "fat_structs.h"
#include "fat_internal.h"

size_t fat_next_in_chain(size_t last_cluster, const superblock_t * sb) {
    kassert(last_cluster >= 2);

    struct fat_info * fi = sb->data;
    if (last_cluster - 2 > fi->data_clusters)
        return -1;

    uint16_t next;

    switch (fi->type) {
        case FAT12:
            kassert(last_cluster < FAT_CLUSTER_END_FAT12);
            off_t fat_offset = last_cluster * 3 / 2;
            if (pread_file(
                sb->fd, &next,
                sizeof(uint16_t),
                fi->fat_start_sector * fi->bytes_per_sector + fat_offset
            ) != sizeof(uint16_t))
                return -1;
            if (last_cluster % 2)
                next >>= 4;
            else
                next &= 0x0FFF;
            if (next > FAT_CLUSTER_END_FAT12)
                next &= ~0x000F; // we want -1 to be an error value
            return next;
        case FAT16:
            kassert(last_cluster < FAT_CLUSTER_END_FAT16);
            if (pread_file(
                sb->fd, &next,
                sizeof(uint16_t),
                fi->fat_start_sector * fi->bytes_per_sector + sizeof(uint16_t) * last_cluster
            ) != sizeof(uint16_t))
                return -1;
            if (next > FAT_CLUSTER_END_FAT16)
                next &= ~0x000F;
            return next;
        case FAT32:
            kassert(last_cluster < FAT_CLUSTER_END_FAT32);
            uint32_t next_32 = 0;
            if (pread_file(
                sb->fd, &next_32,
                sizeof(uint32_t),
                fi->fat_start_sector * fi->bytes_per_sector + sizeof(uint32_t) * last_cluster
            ) != sizeof(uint32_t))
                return -1;
            next_32 &= ~0xF0000000; // reserved bits
            if (next_32 > FAT_CLUSTER_END_FAT32)
                next_32 &= ~0x000F;
            return next_32;
    }
    return -1;
}

int fat_set_chain(size_t last_cluster, size_t next, const superblock_t * sb) {
    kassert(last_cluster >= 2);

    struct fat_info * fi = sb->data;
    if (last_cluster - 2 > fi->data_clusters)
        return -EINVAL;

    next &= ~0xF0000000; // reserved bits on fat32

    if (next == 0)
        fi->last_free_cluster = last_cluster;

    for (size_t i = 0; i < fi->fat_copies; i++) {
        switch (fi->type) {
            case FAT12:
                // read, modify, write
                kassert(last_cluster < FAT_CLUSTER_END_FAT12);

                uint16_t old;
                off_t fat_offset = last_cluster * 3 / 2;
                if (pread_file(
                    sb->fd, &old,
                    sizeof(uint16_t),
                    (fi->fat_start_sector + i*fi->sectors_per_fat)*fi->bytes_per_sector + fat_offset
                ) != sizeof(uint16_t))
                    return -EIO;
                if (last_cluster % 2) {
                    old &= 0x000F;
                    old |= next << 4;
                } else {
                    old &= 0xF000;
                    old |= next & 0x0FFF;
                }
                next = old;
                goto fat16;
            case FAT16:
                kassert(last_cluster < FAT_CLUSTER_END_FAT16);
                fat16:
                if (pwrite_file(
                    sb->fd, &(uint16_t){next},
                    sizeof(uint16_t),
                    (fi->fat_start_sector + i*fi->sectors_per_fat)*fi->bytes_per_sector + sizeof(uint16_t) * last_cluster
                ) != sizeof(uint16_t))
                    return -EIO;
                break;
            case FAT32:
                kassert(last_cluster < FAT_CLUSTER_END_FAT32);
                if (pwrite_file(
                    sb->fd, &(uint32_t){next},
                    sizeof(uint32_t),
                    (fi->fat_start_sector + i*fi->sectors_per_fat)*fi->bytes_per_sector + sizeof(uint32_t) * last_cluster
                ) != sizeof(uint32_t))
                    return -EIO;
        }
    }
    return 0;
}
size_t fat_get_free_cluster(const superblock_t * sb) {
    struct fat_info * fi = sb->data;
    if (fi->last_free_cluster > fi->data_clusters || fi->last_free_cluster < 2)
        fi->last_free_cluster = 2;

    for (size_t cl = fi->last_free_cluster; cl < fi->data_clusters; cl++) {
        size_t next = fat_next_in_chain(cl, sb);
        next &= ~0xF0000000; // reserved on fat32
        if (next == -1)
            return -1;
        if (next == 0) {
            fi->last_free_cluster = next;
            return cl;
        }
    }
    for (size_t cl = 2; cl < fi->last_free_cluster; cl++) {
        size_t next = fat_next_in_chain(cl, sb);
        next &= ~0xF0000000;
        if (next == -1)
            return -1;
        if (next == 0) {
            fi->last_free_cluster = next;
            return cl;
        }
    }
    return 0;
}

int fat_free_chain(size_t first_freed, superblock_t *sb) {
    struct fat_info * fi = sb->data;
    size_t cluster_limit = fi->type == FAT12 ?
        FAT_CLUSTER_END_FAT12 :
        fi->type == FAT16 ?
            FAT_CLUSTER_END_FAT16 :
            FAT_CLUSTER_END_FAT32;

    size_t cl = first_freed;

    while (cl < cluster_limit) {
        size_t next = fat_next_in_chain(cl, sb);
        if (fat_set_chain(cl, 0, sb) != 0) {
            dkprintf("Warning: I/O error on freeing cluster chain from %lu at %lu, rest will become orphaned!\n", first_freed, cl);
            return -EIO;
        }
        if (next == 0) {
            dkprintf("Warning: cluster chain already partially freed\n");
            return 0;
        }
        if (next < 2) {
            dkprintf("Warning: invalid next cluster in cluster chain! %lu @ %lu -> %lu\n", first_freed, cl, next);
            return -EIO;
        }
        cl = next;
    }
    return 0;
}

int fat_end_chain(size_t last_alloced, superblock_t *sb) {
    struct fat_info * fi = sb->data;
    size_t cluster_limit = fi->type == FAT12 ?
        FAT_CLUSTER_END_FAT12 :
        fi->type == FAT16 ?
            FAT_CLUSTER_END_FAT16 :
            FAT_CLUSTER_END_FAT32;

    size_t cl = fat_next_in_chain(last_alloced, sb);
    if (fat_set_chain(last_alloced, cluster_limit, sb) != 0)
        return -EIO;

    return fat_free_chain(cl, sb);
}