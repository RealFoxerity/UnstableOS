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
            if (next_32 > FAT_CLUSTER_END_FAT32)
                next_32 &= ~0x000F;
            return next_32;
    }
    return -1;
}

int fat_set_chain(size_t last_cluster, size_t next, const superblock_t * sb) {
    kassert(last_cluster >= 2);
    kassert(next >= 2);

    struct fat_info * fi = sb->data;
    if (last_cluster - 2 > fi->data_clusters)
        return -EINVAL;

    if (next == 0)
        fi->last_free_cluster = last_cluster;

    for (size_t i = 0; i < fi->fat_copies; i++) {
        switch (fi->type) {
            case FAT12:
                // read, modify, write
                kassert(last_cluster < FAT_CLUSTER_END_FAT12);
                kassert(next < FAT_CLUSTER_END_FAT12);

                uint16_t old;
                off_t fat_offset = last_cluster * 3 / 2;
                if (pread_file(
                    sb->fd, &old,
                    sizeof(uint16_t),
                    (fi->fat_start_sector + i*fi->sectors_per_fat)*fi->bytes_per_sector + fat_offset
                ) != sizeof(uint16_t))
                    return 0;
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
                kassert(next < FAT_CLUSTER_END_FAT16);
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
                kassert(next < FAT_CLUSTER_END_FAT32);
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
        if (next == -1)
            return -1;
        if (next == 0) {
            fi->last_free_cluster = next;
            return cl;
        }
    }
    for (size_t cl = 2; cl < fi->last_free_cluster; cl++) {
        size_t next = fat_next_in_chain(cl, sb);
        if (next == -1)
            return -1;
        if (next == 0) {
            fi->last_free_cluster = next;
            return cl;
        }
    }
    return 0;
}