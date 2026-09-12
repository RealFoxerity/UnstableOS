#include "kernel.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include <string.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <ctype.h>

#include "fs/fs.h"
#include "fs/vfs.h"

#include "structs.h"

#define EXT2_MAX_REPLENISHMENT_COUNT 4 // at once by just calling replenish
#define EXT2_MAX_DISTANCE 40 // max distance between found matches
int ext2_replenish_cache(superblock_t * sb, char for_inodes) {
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    if ((for_inodes && meta->lookaside_cache_inodes_tail >= EXT2_LOOKASIDE_BUFFER_LEN) ||
        (!for_inodes && meta->lookaside_cache_blocks_tail >= EXT2_LOOKASIDE_BUFFER_LEN))
        return 0;

    off_t bgroup_desc_start = meta->block_size;
    if (meta->block_size <= 1024)
        bgroup_desc_start += meta->block_size; // first 1024 bytes are 0

    int found = 0;
    unsigned long original_last_visited = meta->last_visited_bgroup;

    for (unsigned long last_match = 0;
        meta->last_visited_bgroup < meta->block_groups &&
        found < EXT2_MAX_REPLENISHMENT_COUNT &&
        (!found || (found && last_match < EXT2_MAX_DISTANCE));
        meta->last_visited_bgroup ++, last_match ++
    ) {
        if ((for_inodes && meta->lookaside_cache_inodes_tail >= EXT2_LOOKASIDE_BUFFER_LEN) ||
            (!for_inodes && meta->lookaside_cache_blocks_tail >= EXT2_LOOKASIDE_BUFFER_LEN))
            return 0;

        struct ext2_bgroup_desc bgd;
        int ret = pread_file(sb->fd,
            &bgd, sizeof(bgd),
            bgroup_desc_start + meta->last_visited_bgroup * sizeof(bgd));
        if (ret != sizeof(bgd))
            return -EIO;
        if (!bgd.free_inodes && !bgd.free_blocks)
            continue;
        found ++;
        if (meta->lookaside_cache_inodes_tail < EXT2_LOOKASIDE_BUFFER_LEN && bgd.free_inodes)
            meta->lookaside_cache_inodes[meta->lookaside_cache_inodes_tail++] = meta->last_visited_bgroup;
        if (meta->lookaside_cache_blocks_tail < EXT2_LOOKASIDE_BUFFER_LEN && bgd.free_blocks)
            meta->lookaside_cache_blocks[meta->lookaside_cache_blocks_tail++] = meta->last_visited_bgroup;
    }

    if (found == 0 && original_last_visited == 0)
        return -ENOSPC;
    if (meta->last_visited_bgroup >= meta->block_groups)
        meta->last_visited_bgroup = 0;
    if (found == 0)
        return ext2_replenish_cache(sb, for_inodes);
    return 0;
}

ino_t ino_abs(ino_t i) {
    if (i < 0)
        return -i;
    return i;
}

// 0 = no actual inode present, stale cache entry?
// otherwise either block or inode index in this bgroup
static unsigned long ext2_allocate_from_bgroup(superblock_t * sb, unsigned long bgroup, char get_inode) {
    struct ext2_metadata * meta = sb->data;
    struct ext2_bgroup_desc bgd;

    off_t bgroup_desc_start = meta->block_size;
    if (meta->block_size <= 1024)
        bgroup_desc_start += meta->block_size; // first 1024 bytes are 0

    bgroup_desc_start += bgroup * sizeof(bgd);

    int ret = pread_file(sb->fd,
        &bgd, sizeof(bgd),
        bgroup_desc_start);
    if (ret < 0)
        return 0;

    if (get_inode && !bgd.free_inodes)
        return 0;
    if (!get_inode && !bgd.free_blocks)
        return 0;

    unsigned long limit = get_inode ? meta->sb.inodes_per_group : meta->sb.blocks_per_group;
    unsigned long table = get_inode ? bgd.inode_bitmap_block : bgd.block_bitmap_block;

    for (unsigned long i = 0; i < limit; i += 128 * 32) {
        unsigned long buf[128];
        ret = pread_file(sb->fd,
            buf, sizeof(buf),
            table * meta->block_size + (i/32) * sizeof(unsigned long));
        if (ret < sizeof(buf))
            return 0;
        for (int j = 0; j < 128; j++) {
            if (i + j*32 >= limit)
                break;
            if (buf[j] == 0xFFFFFFFF)
                continue;
            unsigned long num = ~buf[j];
            num = __builtin_ctzl(num);
            if (i + j*32 + num >= limit)
                break;
            buf[j] |= 1 << num;
            ret = pwrite_file(sb->fd,
                buf + j, sizeof(unsigned long),
                table * meta->block_size + (i/32 + j) * sizeof(unsigned long));
            if (ret != sizeof(unsigned long))
                continue;
            if (get_inode)
                bgd.free_inodes--;
            else
                bgd.free_blocks--;
            pwrite_file(sb->fd,
                &bgd, sizeof(bgd),
                bgroup_desc_start);
            return bgroup * limit + i + j*32 + num;
        }
    }

    // was incorrectly marked as free
    if (get_inode)
        bgd.free_inodes = 0;
    else
        bgd.free_blocks = 0;

    pwrite_file(sb->fd,
                &bgd, sizeof(bgd),
                bgroup_desc_start);
    return 0;
}

#define BGROUP_LOCALITY 4 // in each direction
// operations in increasing order of time complexity
unsigned long ext2_allocate(superblock_t * sb, ino_t ideal_locality, char get_inode) {
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    unsigned long ideal_bgroup = ideal_locality / (get_inode ? meta->sb.inodes_per_group : meta->sb.blocks_per_group);
    unsigned long * freed_tail = get_inode ? &meta->last_freed_inodes_tail : &meta->last_freed_blocks_tail;
    unsigned long * freed_list = get_inode ? meta->last_freed_inodes : meta->last_freed_blocks;
    unsigned long freed_locality = get_inode ? BGROUP_LOCALITY * meta->sb.inodes_per_group : BGROUP_LOCALITY;

    int result_offset = get_inode ? 1 : meta->block_size > 1024 ? 0 : 1;

    for (int i = 0; i < *freed_tail; i++) {
        if (ino_abs(ideal_locality - freed_list[i]) >= freed_locality)
                continue;

        memcpy(freed_list + i, freed_list + i + 1,
            (EXT2_LAST_BUFFER_LEN - i - 1) * sizeof(unsigned long));
        (*freed_tail)--;
        if (get_inode)
            meta->sb.free_inodes--;
        else
            meta->sb.free_blocks--;
        return freed_list[i] + result_offset;
    }

    unsigned long * lookaside_tail = get_inode ? &meta->lookaside_cache_inodes_tail : &meta->lookaside_cache_blocks_tail;
    unsigned long * lookaside_list = get_inode ? meta->lookaside_cache_inodes : meta->lookaside_cache_blocks;

    for (int i = 0; i < *lookaside_tail; i++) {
        if (ino_abs((ino_t)lookaside_list[i] - ideal_bgroup) >= BGROUP_LOCALITY)
            continue;
        unsigned long ret = ext2_allocate_from_bgroup(sb, lookaside_list[i], get_inode);
        if (ret == 0) {
            memcpy(lookaside_list + i, lookaside_list + i + 1,
                (EXT2_LOOKASIDE_BUFFER_LEN - i - 1) * sizeof(unsigned long));
            (*lookaside_tail)--;
            i--;
        }
        if (get_inode)
            meta->sb.free_inodes--;
        else
            meta->sb.free_blocks--;
        return ret + result_offset;
    }

    for (unsigned long bgroup = ideal_bgroup > BGROUP_LOCALITY ? ideal_bgroup - BGROUP_LOCALITY : 0;
        bgroup < ideal_bgroup + BGROUP_LOCALITY;
        bgroup++
    ) {
        unsigned long ret = ext2_allocate_from_bgroup(sb, bgroup, get_inode);
        if (ret) {
            if (get_inode)
                meta->sb.free_inodes--;
            else
                meta->sb.free_blocks--;
            return ret + result_offset;
        }
    }

    while (1) {
        if (ext2_replenish_cache(sb, get_inode) == -ENOSPC)
            return 0;

        while (*lookaside_tail) {
            unsigned long ret = ext2_allocate_from_bgroup(sb, lookaside_list[*lookaside_tail - 1], get_inode);
            (*lookaside_tail)--;
            if (ret) {
                if (get_inode)
                    meta->sb.free_inodes--;
                else
                    meta->sb.free_blocks--;
                return ret + result_offset;
            }
        }
    }
}

void ext2_free(superblock_t * sb, unsigned long block, char get_inode) {
    // while 0 might be a valid target, it's reserved the absolute majority of time
    // so this allows us to skip needless != 0 checks
    if (block == 0)
        return;
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    if (get_inode)
        block--;
    struct ext2_bgroup_desc bgd;
    off_t bgroup_desc_start = meta->block_size;
    if (meta->block_size <= 1024) {
        bgroup_desc_start += meta->block_size; // first 1024 bytes are 0
        if (!get_inode)
            block --;
    }

    unsigned long limit = get_inode ? meta->sb.inodes_per_group : meta->sb.blocks_per_group;
    unsigned long bgroup = block / limit;
    bgroup_desc_start += bgroup * sizeof(bgd);

    int ret = pread_file(sb->fd,
        &bgd, sizeof(bgd),
        bgroup_desc_start);
    if (ret < 0)
        return;

    unsigned long table = get_inode ? bgd.inode_bitmap_block : bgd.block_bitmap_block;
    unsigned long table_offset = block % limit;

    unsigned long bitmap_fragment;
    ret = pread_file(sb->fd,
        &bitmap_fragment, sizeof(bitmap_fragment),
        table * meta->block_size + (table_offset / 32) * sizeof(unsigned long));
    if (ret < 0)
        return;
    bitmap_fragment &= ~(1 << (table_offset % 32));
    ret = pwrite_file(sb->fd,
        &bitmap_fragment, sizeof(bitmap_fragment),
        table * meta->block_size + (table_offset / 32) * sizeof(unsigned long));
    if (ret < 0)
        return;
    if (get_inode) {
        bgd.free_inodes++;
        meta->sb.free_inodes++;
    } else {
        bgd.free_blocks++;
        meta->sb.free_blocks++;
    }
    pwrite_file(sb->fd,
        &bgd, sizeof(bgd),
        bgroup_desc_start);

    if (get_inode) {
        if (meta->last_freed_inodes_tail < EXT2_LAST_BUFFER_LEN)
            meta->last_freed_inodes[meta->last_freed_inodes_tail++] = block;
    } else if (meta->last_freed_blocks_tail < EXT2_LAST_BUFFER_LEN)
            meta->last_freed_blocks[meta->last_freed_blocks_tail++] = block;
}


// left = how many blocks to leave unfreed
// 0 = success
int ext2_free_indirect(superblock_t * sb, unsigned long block, unsigned long left) {
    if (block == 0)
        return 0;
    kassert(sb);
    kassert(sb->data);

    struct ext2_metadata * meta = sb->data;
    unsigned long temp;
    for (unsigned long i = left; i < meta->block_size / 4; i++) {
        if (pread_file(sb->fd,
            &temp, sizeof(temp),
            block * meta->block_size + i * sizeof(unsigned long))
            != sizeof(unsigned long))
                return -EIO;
        if (temp == 0)
            continue;
        ext2_free(sb, temp, 0);
        temp = 0;
        if (pwrite_file(sb->fd,
            &temp, sizeof(temp),
            block * meta->block_size + i * sizeof(unsigned long))
            != sizeof(unsigned long))
                return -EIO;
    }
    return 0;
}

int ext2_free_doubly_indirect(superblock_t * sb, unsigned long block, unsigned long left) {
    if (block == 0)
        return 0;
    kassert(sb);
    kassert(sb->data);

    struct ext2_metadata * meta = sb->data;
    unsigned long temp;
    unsigned long left_indirects = left/(meta->block_size / 4);
    for (unsigned long i = left_indirects; i < meta->block_size / 4; i++) {
        if (pread_file(sb->fd,
            &temp, sizeof(temp),
            block * meta->block_size + i * sizeof(unsigned long))
            != sizeof(unsigned long))
                return -EIO;
        if (temp == 0)
            continue;
        ext2_free_indirect(sb, temp, left % (meta->block_size / 4));
        if (left % (meta->block_size / 4) == 0) {
            ext2_free(sb, temp, 0);
            temp = 0;
            if (pwrite_file(sb->fd,
                &temp, sizeof(temp),
                block * meta->block_size + i * sizeof(unsigned long))
                != sizeof(unsigned long))
                    return -EIO;
        } else
            left = 0;
    }
    return 0;
}

int ext2_free_triply_indirect(superblock_t * sb, unsigned long block, unsigned long left) {
    if (block == 0)
        return 0;
    kassert(sb);
    kassert(sb->data);

    struct ext2_metadata * meta = sb->data;
    unsigned long temp;
    unsigned long left_dindirects = left/(meta->block_size / 4)/(meta->block_size / 4);
    for (unsigned long i = left_dindirects; i < meta->block_size / 4; i++) {
        if (pread_file(sb->fd,
            &temp, sizeof(temp),
            block * meta->block_size + i * sizeof(unsigned long))
            != sizeof(unsigned long))
                return -EIO;
        if (temp == 0)
            continue;
        ext2_free_doubly_indirect(sb, temp, left % ((meta->block_size / 4)*(meta->block_size / 4)));
        if (left % ((meta->block_size / 4)*(meta->block_size / 4)) == 0) {
            ext2_free(sb, temp, 0);
            temp = 0;
            if (pwrite_file(sb->fd,
                &temp, sizeof(temp),
                block * meta->block_size + i * sizeof(unsigned long))
                != sizeof(unsigned long))
                    return -EIO;
        } else
            left = 0;
    }
    return 0;
}

// so we can chain this and allocate on a single line
static unsigned long ext2_zero_block(superblock_t * sb, ino_t block) {
    if (block == 0)
        return 0;
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;
    static const unsigned char zeroblock[512] = {0};
    for (size_t i = 0; i < meta->block_size / sizeof(zeroblock); i++)
        pwrite_file(sb->fd, zeroblock, sizeof(zeroblock),
            block * meta->block_size + i * sizeof(zeroblock));
    return block;
}

// if is_blockno is set, target offset specifies a block number instead of an offset into the file
// potentially easier on for loops
// required to flush inode externally
unsigned long ext2_get_block(superblock_t * sb, struct ext2_inode * inode, off_t target_offset, char is_blockno, char alloc) {
    kassert(sb);
    kassert(inode);
    if (target_offset < 0)
        return 0;
    struct ext2_metadata * meta = sb->data;
    if (!is_blockno)
        target_offset /= meta->block_size;
    // direct pointers are enough
    if (target_offset < 12) {
        if (inode->blocks[target_offset] || !alloc)
            return inode->blocks[target_offset];
        unsigned long ret = inode->blocks[target_offset] = ext2_zero_block(sb, ext2_allocate(sb, inode->blocks[0], 0));
        if (ret)
            inode->used_512blocks += meta->block_size / 512;
        return ret;
    }

    // indirect block
    target_offset -= 12;
    unsigned long block = inode->indirect_block; // to do gotos
    if (target_offset < meta->block_size / 4) {
        if (block == 0 && alloc)
            block = inode->indirect_block = ext2_zero_block(sb, ext2_allocate(sb, inode->blocks[0], 0));
        indirect:
        if (block == 0)
            return 0;
        off_t block_offset = block * meta->block_size;

        block_offset += target_offset * 4;
        unsigned long out = 0;
        // error will lead to out still being 0
        if (pread_file(sb->fd,
            &out, sizeof(out),
            block_offset) == sizeof(out) &&
            out == 0 && alloc
        ) {
            out = ext2_zero_block(sb, ext2_allocate(sb, inode->blocks[0], 0));
            if (out == 0)
                return 0;
            pwrite_file(sb->fd,
                &out, sizeof(out),
                block_offset);
            inode->used_512blocks += meta->block_size / 512;
        }
        return out;
    }

    // doubly indirect
    target_offset -= meta->block_size / 4;
    block = inode->d_indirect_block;
    if (target_offset < (meta->block_size / 4) * meta->block_size / 4) {
        if (block == 0 && alloc)
            block = inode->d_indirect_block = ext2_zero_block(sb, ext2_allocate(sb, inode->blocks[0], 0));
        double_indirect:
        if (block == 0)
            return 0;
        off_t block_offset = block * meta->block_size;

        block_offset += (target_offset / (meta->block_size / 4)) * 4;
        target_offset = target_offset % (meta->block_size / 4);
        unsigned long out = 0;
        // error will lead to out still being 0
        if (pread_file(sb->fd,
            &out, sizeof(out),
            block_offset) == sizeof(out) &&
            out == 0 && alloc
        ) {
            out = ext2_zero_block(sb, ext2_allocate(sb, inode->blocks[0], 0));
            if (out == 0)
                return 0;
            pwrite_file(sb->fd,
                &out, sizeof(out),
                block_offset);
        }
        block = out;
        goto indirect;
    }

    // triply indirect
    target_offset -= (meta->block_size / 4) * meta->block_size / 4;
    if (target_offset >= (meta->block_size / 4) * (meta->block_size / 4) * meta->block_size / 4)
        return 0; // file larger than max supported for this block size

    if (inode->t_indirect_block == 0)
        return 0;
    off_t block_offset = inode->t_indirect_block * meta->block_size;
    block_offset += (target_offset / (meta->block_size / 4) / (meta->block_size / 4)) * 4;
    target_offset = target_offset % ((meta->block_size / 4) * meta->block_size / 4);

    unsigned long out = 0;
    // error will lead to out still being 0
    if (pread_file(sb->fd,
        &out, sizeof(out),
        block_offset) == sizeof(out) &&
        out == 0 && alloc
    ) {
        out = ext2_zero_block(sb, ext2_allocate(sb, inode->blocks[0], 0));
        if (out == 0)
            return 0;
        pwrite_file(sb->fd,
            &out, sizeof(out),
            block_offset);
    }
    block = out;
    goto double_indirect;
}

void ext2_adjust_bgroup_dir_count(superblock_t * sb, unsigned long ino, short delta) {
    if (ino == 0 || delta == 0)
        return;
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    ino--;
    struct ext2_bgroup_desc bgd;
    off_t bgroup_desc_start = meta->block_size;
    if (meta->block_size <= 1024)
        bgroup_desc_start += meta->block_size; // first 1024 bytes are 0

    unsigned long bgroup = ino / meta->sb.inodes_per_group;
    bgroup_desc_start += bgroup * sizeof(bgd);

    int ret = pread_file(sb->fd,
        &bgd, sizeof(bgd),
        bgroup_desc_start);
    if (ret < 0)
        return;
    bgd.used_dir_inodes += delta;
    pwrite_file(sb->fd,
        &bgd, sizeof(bgd),
        bgroup_desc_start);
}

int ext2_alloc_dentry(superblock_t * sb, struct ext2_inode * dir, const char * name, ino_t ino, mode_t file_type) {
    kassert(sb);
    kassert(dir);
    kassert(sb->data);
    kassert(name);
    kassert(ino > 0);

    size_t name_len = strnlen(name, 256);
    if (name_len > 255)
        return -ENAMETOOLONG;

    struct ext2_metadata * meta = sb->data;

    switch (IFTODT(file_type)) {
        default:
        case DT_UNKNOWN:
            file_type = EXT2_FT_UNK;
            break;
        case DT_REG:
            file_type = EXT2_FT_REG;
            break;
        case DT_DIR:
            file_type = EXT2_FT_DIR;
            break;
        case DT_BLK:
            file_type = EXT2_FT_BLK;
            break;
        case DT_CHR:
            file_type = EXT2_FT_CHR;
            break;
        case DT_FIFO:
            file_type = EXT2_FT_FIFO;
            break;
        case DT_LNK:
            file_type = EXT2_FT_LNK;
            break;
        case DT_SOCK:
            file_type = EXT2_FT_SOCK;
            break;
    }

    if (dir->blocks[0] == 0) {
        dkprintf("0 length directory inode, broken filesystem?\n");
        return -EIO;
    }

    size_t needed_size = sizeof(struct ext2_directory) + name_len;
    needed_size += 3;
    needed_size &= ~3;

    for (size_t i = 0; i < UINT32_MAX; i++) {
        unsigned long block = ext2_get_block(sb, dir, i, 1, 1);
        if (block == 0)
            return -ENOSPC;
        for (unsigned long j = 0; j < meta->block_size - needed_size;) {
            struct ext2_directory dentry;
            int ret = pread_file(sb->fd,
                &dentry, sizeof(dentry),
                block * meta->block_size + j);
            if (ret < sizeof(dentry))
                return -EIO;

            if (dentry.inode == 0 && dentry.rec_len == 0) {
                if (j != 0) {
                    dkprintf("0 length directory entry, broken filesystem?\n");
                    return -EIO;
                }
                dentry.rec_len = meta->block_size;
            }

            if (dentry.inode == 0 && dentry.rec_len >= needed_size) {
                write_new:
                dentry.inode = ino;
                if (meta->filetype)
                    dentry.file_type = file_type;
                else
                    dentry.file_type = 0;
                dentry.name_len = name_len;
                ret = pwrite_file(sb->fd,
                    &dentry, sizeof(dentry),
                    block * meta->block_size + j);
                if (ret < sizeof(dentry))
                    return -EIO;
                ret = pwrite_file(sb->fd,
                    name, name_len,
                    block * meta->block_size + j + sizeof(struct ext2_directory));
                if (ret < name_len)
                    return -EIO;
                return 0;
            }
            size_t aligned_size = sizeof(struct ext2_directory) + dentry.name_len;
            aligned_size += 3;
            aligned_size &= ~3;

            if (dentry.rec_len >= aligned_size + needed_size) {
                size_t new_len = dentry.rec_len - aligned_size;
                dentry.rec_len = aligned_size;

                ret = pwrite_file(sb->fd,
                    &dentry, sizeof(dentry),
                    block * meta->block_size + j);
                if (ret < sizeof(dentry))
                    return -EIO;
                j += aligned_size;
                dentry.rec_len = new_len;
                goto write_new;
            }

            j += dentry.rec_len;
        }
    }
    return -ENOSPC;
}
