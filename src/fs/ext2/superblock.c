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
#define dkprintf(fmt, ...) kprintf("ext2: "fmt, ##__VA_ARGS__)

// TODO: add fragment checking if/when adding fragment support
// note: neither Linux, BSD, or HURD support fragments :p
int ext2_init(superblock_t * sb) {
    kassert(sb);
    kassert(sb->fd);
    struct ext2_metadata * meta = kalloc(sizeof(struct ext2_metadata));
    if (!meta)
        return -ENOMEM;
    memset(meta, 0, sizeof(struct ext2_metadata));

    if (pread_file(sb->fd, &meta->sb, sizeof(struct ext2_sb), 1024) != sizeof(struct ext2_sb)) {
        kfree(meta);
        return -EIO;
    }
    if (meta->sb.magic != EXT2_MAGIC) {
        kfree(meta);
        dkprintf("Invalid superblock magic!\n");
        return -EIO;
    }
    if (meta->sb.total_blocks < EXT2_MIN_BLOCKS) {
        kfree(meta);
        dkprintf("Suspiciously low block count, is the volume okay?\n");
        return -EIO;
    }
    if (meta->sb.total_inodes < EXT2_MIN_INODES) {
        kfree(meta);
        dkprintf("Suspiciously low inode count, is the volume okay?\n");
        return -EIO;
    }
    if (meta->sb.blocks_per_group < EXT2_MIN_BLOCK_PER_GROUP) {
        kfree(meta);
        dkprintf("Refusing to mount an ext2 volume with less than %d blocks per group!\n", EXT2_MIN_BLOCK_PER_GROUP);
        return -EIO;
    }
    if (meta->sb.block_size_shift > EXT2_MAX_SHIFT) {
        kfree(meta);
        dkprintf("Refusing to mount an ext2 volume with blocks greater than 262KiB!\n");
        return -EIO;
    }
    if (meta->sb.fragment_size_shift > EXT2_MAX_SHIFT) {
        kfree(meta);
        dkprintf("Refusing to mount an ext2 volume with fragments greater than 262KiB!\n");
        return -EIO;
    }
    if (meta->sb.reserved_blocks >= meta->sb.total_blocks) {
        kfree(meta);
        dkprintf("Invalid reserved blocks count!\n");
        return -EIO;
    }
    if (meta->sb.total_blocks <= meta->sb.total_inodes ||
        meta->sb.total_inodes / meta->sb.inodes_per_group != // can't be truncated as it's first in the bgroup
        (meta->sb.total_blocks + meta->sb.blocks_per_group - 1) / meta->sb.blocks_per_group
    ) {
        kfree(meta);
        dkprintf("Invalid total block and/or total inode count!\n");
        return -EIO;
    }
    if (meta->sb.inode_size < sizeof(struct ext2_inode)) {
        kfree(meta);
        dkprintf("Invalid inode size!\n");
        return -EIO;
    }
    if (meta->sb.first_nonresv_inode >= meta->sb.total_inodes ||
        meta->sb.first_nonresv_inode < 6) {
        kfree(meta);
        dkprintf("Invalid reserved inode count!\n");
        return -EIO;
    }

    if (meta->sb.version_major > 1 || (meta->sb.version_major == 1 && meta->sb.version_minor > 0)) {
        kfree(meta);
        dkprintf("Unsupported ext version (%d.%hd)!\n", meta->sb.version_major, meta->sb.version_minor);
        return -EIO;
    }

    if (meta->sb.required_features.compression) {
        kfree(meta);
        dkprintf("Compressed volumes are not supported!\n");
        return -EIO;
    }
    if (meta->sb.required_features.journal_replay ||
        meta->sb.required_features.journal_device) {
        kfree(meta);
        dkprintf("Journalled volumes are not yet supported and this volume requires journal to read!\n");
        return -EIO;
    }
    if (meta->sb.required_features.meta_bgroup) {
        kfree(meta);
        dkprintf("Volumes with meta block groups are not yet supported!\n");
        return -EIO;
    }
    if (meta->sb.required_features.unknown) {
        kfree(meta);
        dkprintf("Unrecognized required features specified, refusing to mount!\n");
        return -EIO;
    }

    if (meta->sb.optional_features.journal) {
        dkprintf("Journalled volumes are not yet supported, mounting read-only to be safe!\n");
        sb->mount_options |= MOUNT_RDONLY;
    }

    if (meta->sb.required_rw_features.dir_btree) {
        dkprintf("Volumes with directory binary trees are not yet supported, mounting read-only!\n");
        sb->mount_options |= MOUNT_RDONLY;
    }
    if (meta->sb.required_rw_features.unknown) {
        dkprintf("Unrecognized required features to write, mounting read-only to be safe!\n");
        sb->mount_options |= MOUNT_RDONLY;
    }
    if (meta->sb.os_id > 1)
        dkprintf("Unrecognized creator OS, uid/gid might be wrong and get truncated to 16 bits!\n");

    if (meta->sb.version_major >= 1) {
        dkprintf("Mounted ext2 volume `%16s` last mounted on `%64s`, free %u/%u MiB\n",
            meta->sb.label, meta->sb.last_mountpoint,
            (meta->sb.block_size_shift + 1) * (meta->sb.free_blocks  / 1024),
            (meta->sb.block_size_shift + 1) * (meta->sb.total_blocks / 1024));
    }
    if (meta->sb.mounts_since_fsck >= meta->sb.mounts_per_fsck ||
        (meta->sb.fsck_interval &&
            system_time_sec - meta->sb.last_fsck_time > meta->sb.fsck_interval)) {
        dkprintf("Recommended to run fsck when convienient\n");
    }
    if (meta->sb.fs_state == EXT2_FS_ERROR) {
        dkprintf("Volume state with errors, recommended to run fsck when convienient, mounting read-only!\n");
        sb->mount_options |= MOUNT_RDONLY;
    }

    meta->inode_size = meta->sb.inode_size;
    meta->block_size = 1024 << meta->sb.block_size_shift;
    meta->block_groups = (meta->sb.total_blocks + meta->sb.blocks_per_group - 1) / meta->sb.blocks_per_group;

    meta->sparse_sb = meta->sb.required_rw_features.sparse_sb;
    meta->large_files = meta->sb.required_rw_features.large_files;
    meta->filetype = meta->sb.required_features.filetype;

    meta->sb.mounts_since_fsck++;
    meta->sb.mount_time = (int32_t)system_time_sec;

    sb->data = meta;
    strcpy(meta->sb.last_mountpoint, "TODO: Implement last mountpoint into UnstableOS");
    return 0;
}

int ext2_deinit(superblock_t * sb) {
    kassert(sb);
    kfree(sb->data);
    return 0;
}

// lock if looking up, lockless if inode already exists
static int ext2_get_inode(const superblock_t * sb, ino_t ino, struct ext2_inode * out) {
    kassert(ino > 0);
    kassert(sb);
    kassert(out);

    struct ext2_metadata * meta = sb->data;
    kassert(meta);

    unsigned long block_group = (ino - 1) / meta->sb.inodes_per_group;
    unsigned long block_index = (ino - 1) % meta->sb.inodes_per_group;
    if (block_group >= meta->block_groups)
        return -EIO;

    // first block after the superblock
    off_t bgroup_desc_start = meta->block_size + block_group * sizeof(struct ext2_bgroup_desc);
    if (meta->block_size <= 1024)
        bgroup_desc_start += meta->block_size; // first 1024 bytes are 0

    struct ext2_bgroup_desc bgd;
    if (pread_file(sb->fd,
        &bgd, sizeof(struct ext2_bgroup_desc),
        bgroup_desc_start) != sizeof(struct ext2_bgroup_desc))
            return -EIO;

    off_t inode_off = bgd.inode_table_block * meta->block_size + block_index * meta->sb.inode_size;

    if (pread_file(sb->fd,
        out, sizeof(struct ext2_inode),
        inode_off) != sizeof(struct ext2_inode))
            return -EIO;
    return 0;
}

// if is_blockno is set, target offset specifies a block number instead of an offset into the file
// potentially easier on for loops
static unsigned long ext2_get_block(const superblock_t * sb, const struct ext2_inode * inode, off_t target_offset, char is_blockno) {
    kassert(sb);
    kassert(inode);
    if (target_offset < 0)
        return 0;
    struct ext2_metadata * meta = sb->data;
    if (!is_blockno)
        target_offset /= meta->block_size;
    // direct pointers are enough
    if (target_offset < 12)
        return inode->blocks[target_offset];

    // indirect block
    target_offset -= 12;
    unsigned long block = inode->indirect_block; // to do gotos
    if (target_offset < meta->block_size / 4) {
        indirect:
        if (block == 0)
            return 0;
        off_t block_offset = block * meta->block_size;

        block_offset += target_offset * 4;
        unsigned long out = 0;
        // error will lead to out still being 0
        pread_file(sb->fd,
            &out, sizeof(out),
            block_offset);
        return out;
    }

    // doubly indirect
    target_offset -= meta->block_size / 4;
    block = inode->d_indirect_block;
    if (target_offset < (meta->block_size / 4) * meta->block_size / 4) {
        double_indirect:
        if (block == 0)
            return 0;
        off_t block_offset = block * meta->block_size;

        block_offset += (target_offset / (meta->block_size / 4)) * 4;
        target_offset = target_offset % (meta->block_size / 4);
        unsigned long out = 0;
        // error will lead to out still being 0
        pread_file(sb->fd,
            &out, sizeof(out),
            block_offset);
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
    pread_file(sb->fd,
        &out, sizeof(out),
        block_offset);
    block = out;
    goto double_indirect;
}

int ext2_lookup(superblock_t * sb, inode_t * last, const char * pathname, inode_t ** inode_out, unsigned short flags) {
    kassert(sb);
    kassert(sb->data);
    if (last && !S_ISDIR(last->mode))
        return -ENOTDIR;
    if (last && strcmp(pathname, ".") == 0) {
        *inode_out = last;
        __atomic_add_fetch(&last->instances, 1, __ATOMIC_ACQUIRE);
        return 0;
    }
    ino_t base_inode = last ? last->id : EXT2_ROOT_INO;

    if (base_inode <= 0) {
        dkprintf("Invalid last inode number (%lld)\n", base_inode);
        return -EIO;
    }

    if (!*pathname)
        return -EILSEQ;
    if (base_inode == EXT2_ROOT_INO && strcmp(pathname, "..") == 0)
        return VFS_LOOKUP_ESCAPE;

    if (strlen(pathname) > 255)
        return -ENAMETOOLONG;

    if (check_eintr())
        return -EINTR;

    struct ext2_metadata * meta = sb->data;
    sigset_t sig = PAUSE_SIGNALS();

    struct ext2_inode ino;
    // hopefully lockless?
    int ret = ext2_get_inode(sb, base_inode, &ino);
    if (ret < 0) goto end;

    if (!S_ISDIR(ino.mode)) {
        ret = -ENOTDIR;
        goto end;
    }

    struct ext2_directory dirent = {0};
    char name[256] = {0};
    rw_spinlock_acquire_read(&meta->access_lock);
    for (size_t i = 0; ; i++) {
        unsigned long block = ext2_get_block(sb, &ino, i, 1);
        if (block == 0) {
            rw_spinlock_release_read(&meta->access_lock);
            ret = -ENOENT;
            goto end;
        }
        for (size_t offset = 0; offset < meta->block_size;) {
            if (pread_file(sb->fd,
                &dirent, sizeof(struct ext2_directory),
                block * meta->block_size + offset) != sizeof(struct ext2_directory) ||
                dirent.name_len > dirent.rec_len - 8
            ) {
                rw_spinlock_release_read(&meta->access_lock);
                ret = -EIO;
                goto end;
            }
            if (pread_file(sb->fd,
                name, dirent.name_len,
                block * meta->block_size + offset + sizeof(struct ext2_directory)
                ) != dirent.name_len
            ) {
                rw_spinlock_release_read(&meta->access_lock);
                ret = -EIO;
                goto end;
            }
            name[dirent.name_len] = '\0';
            if (dirent.inode != 0 && strncmp(pathname, name, 255) == 0)
                goto found;
            if (dirent.rec_len == 0) {
                dkprintf("Warning: directory entry with record length 0, skipping rest of block %lu\n", block);
                break;
            }
            offset += dirent.rec_len;
            if (offset % 4)
                dkprintf("Warning: Unaligned directory entry at block %lu\n", block);
        }
    }
    found:
    if (dirent.inode == 0)
        ret = -ENOENT;
    else if (meta->filetype &&
        flags & O_DIRECTORY &&
        dirent.file_type != EXT2_FT_DIR
    ) {
        ret = -ENOTDIR;
        dirent.inode = 0;
    }
    if (dirent.inode) {
        ret = ext2_get_inode(sb, dirent.inode, &ino);
    }
    rw_spinlock_release_read(&meta->access_lock);
    if (ret == 0) {
        off_t size = ino.size;
        if (dirent.file_type == EXT2_FT_REG)
            if (meta->large_files)
                size = (off_t)(ino.lo_size | ((uint64_t)ino.hi_size << 32));
        inode_t new_inode = {
            .backing_superblock = sb,
            .id = dirent.inode,
            .mode = ino.mode,
            .uid = ino.uid | (ino.osd2_linux.uid_high << 16),
            .gid = ino.gid | (ino.osd2_linux.gid_high << 16),
            .nlink = ino.nlink,
            .btime = ino.btime,
            .ctime = ino.mtime, // wrong, but yk, at least something
            .mtime = ino.mtime,
            .atime = ino.atime,
            .size  = size,
            .io_block_size = (blksize_t)meta->block_size,
        };
        if (S_ISBLK(new_inode.mode) || S_ISCHR(new_inode.mode))
            new_inode.device = ino.blocks[0] ? ino.blocks[0] : ino.blocks[1];
        if (S_ISFIFO(ino.mode)) {
            ret = -ENXIO;
            dkprintf("Named pipes are not yet supported\n");
            goto end;
        }
        if (S_ISSOCK(ino.mode)) {
            ret = -ENXIO;
            dkprintf("Named sockets are not yet supported\n");
            goto end;
        }
        if (S_ISLNK(ino.mode)) {
            ret = -ENXIO;
            dkprintf("Symlinks are not yet supported\n");
            goto end;
        }
        ret = register_inode(&new_inode, inode_out, flags);
    }
    end:
    RESTORE_SIGNALS(sig);
    return ret;
}

// offset here is 0 <32 bits of block number> <31 bits of offset into this block>
// has to be like this because off_t is signed, 31 bits is much more than enough (in fact 18 would be enough)
ssize_t ext2_readdir(file_descriptor_t * fd, struct dirent * dent, size_t dent_size, off_t offset) {
    if (!dent)
        return -EFAULT;
    if (offset < 0)
        return -ENOENT;

    kassert(fd);
    kassert(fd->inode);
    superblock_t * sb = fd->inode->backing_superblock;
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    unsigned long block_number = (uint64_t)offset >> 31;
    unsigned long block_offset = offset & 0x7FFFFFFF;

    if (block_offset >= meta->block_size - sizeof(struct ext2_directory) - 2)
        return -ENOENT;

    if (check_eintr())
        return -EINTR;

    struct ext2_inode ino;

    sigset_t sig = PAUSE_SIGNALS();
    int ret = ext2_get_inode(sb, fd->inode->id, &ino);
    if (ret < 0)
        goto end;

    rw_spinlock_acquire_read(&meta->access_lock);
    unsigned long block = ext2_get_block(sb, &ino, block_number, 1);
    if (block == 0) {
        ret = 0;
        goto end2;
    }

    struct ext2_directory dir = {0};
    unsigned long actual_offset;
    // get the closest greater-than-equal directory entry
    // this is here so that unlink races aren't such a big issue and so that seekdir is safe(r)
    for (actual_offset = 0; actual_offset < block_offset;) {
        if (pread_file(sb->fd,
            &dir, sizeof(struct ext2_directory),
            block * meta->block_size + actual_offset)
            != sizeof(struct ext2_directory)) {
            ret = -EIO;
            goto end2;
        }
        if (dir.rec_len == 0) {
            dkprintf("Warning: directory entry with record length 0, skipping rest of block %lu\n", block);
            break;
        }
        actual_offset += dir.rec_len;
        if (offset % 4)
            dkprintf("Warning: Unaligned directory entry at block %lu\n", block);
    }
    // slightly paranoid approach just in case
    if ((dir.rec_len == 0 && block_offset) || actual_offset >= meta->block_size - sizeof(struct ext2_directory) - 2) {
        block_number++;
        block = ext2_get_block(sb, &ino, block_number, 1);
        if (block == 0) {
            ret = 0;
            goto end2;
        }
        actual_offset = 0;
    }
    if (pread_file(sb->fd,
        &dir, sizeof(struct ext2_directory),
        block * meta->block_size + actual_offset)
        != sizeof(struct ext2_directory)
    ) {
        ret = -EIO;
        goto end2;
    }
    if (dent_size < sizeof(struct dirent) + dir.name_len + 1) {
        ret = -EINVAL;
        goto end2;
    }
    dent->d_ino = dir.inode;
    dent->d_off = (off_t)(actual_offset | ((uint64_t)block_number << 31));
    dent->d_reclen = sizeof(struct dirent) + dir.name_len + 1;
    if (pread_file(sb->fd,
        dent->d_name, dir.name_len,
        block * meta->block_size + actual_offset + sizeof(struct ext2_directory)) != dir.name_len
    ) {
        ret = -EIO;
        goto end2;
    }
    dent->d_name[dir.name_len] = '\0';
    if (meta->filetype) {
        switch (dir.file_type) {
            default:
            case EXT2_FT_UNK:
                dent->d_type = DT_UNKNOWN;
                break;
            case EXT2_FT_REG:
                dent->d_type = DT_REG;
                break;
            case EXT2_FT_DIR:
                dent->d_type = DT_DIR;
                break;
            case EXT2_FT_CHR:
                dent->d_type = DT_CHR;
                break;
            case EXT2_FT_BLK:
                dent->d_type = DT_BLK;
                break;
            case EXT2_FT_FIFO:
                dent->d_type = DT_FIFO;
                break;
            case EXT2_FT_SOCK:
                dent->d_type = DT_SOCK;
                break;
            case EXT2_FT_LNK:
                dent->d_type = DT_LNK;
                break;
        }
    } else {
        ret = ext2_get_inode(sb, dir.inode, &ino);
        if (ret >= 0) {
            dent->d_type = IFTODT(ino.mode);
        }
    }

    actual_offset += dir.rec_len;
    if (actual_offset >= meta->block_size - sizeof(struct ext2_directory) - 2) {
        actual_offset = 0;
        block_number++;
    }
    rw_spinlock_acquire_write(&fd->access_lock);
    fd->off = (off_t)(actual_offset | ((uint64_t)block_number << 31));
    rw_spinlock_release_write(&fd->access_lock);
    ret = dent->d_reclen;

    end2:
    rw_spinlock_release_read(&meta->access_lock);

    end:
    RESTORE_SIGNALS(sig);
    return ret;
}

ssize_t ext2_pread(file_descriptor_t * fd, void * buf, size_t n, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->backing_superblock);
    superblock_t * sb = fd->inode->backing_superblock;
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;
    if (!buf)
        return -EFAULT;

    if (offset < 0) return -EINVAL;
    if (!S_ISREG(fd->inode->mode)) return -EINVAL;
    if (offset >= fd->inode->size) return 0;
    if (n == 0) return 0;
#ifdef E2BIG_ON_2G
    if (n > SSIZE_MAX) return -E2BIG;
#else
    if (n > SSIZE_MAX) n = SSIZE_MAX;
#endif

    if (check_eintr())
        return -EINTR;
    sigset_t sig = PAUSE_SIGNALS();
    struct ext2_inode ino;
    ssize_t ret = ext2_get_inode(sb, fd->inode->id, &ino);
    if (ret < 0)
        goto end;

    rw_spinlock_acquire_read(&meta->access_lock);
    size_t read_bytes = 0;
    for (; read_bytes < n && offset < fd->inode->size;) {
        unsigned long block = ext2_get_block(sb, &ino, offset, 0);
        size_t remaining_of_block = meta->block_size - offset % meta->block_size;
        size_t to_read = remaining_of_block > n - read_bytes ? n - read_bytes : remaining_of_block;
        if (offset + to_read > fd->inode->size)
            to_read = fd->inode->size - offset;

        // sparse file
        if (block == 0)
            memset(buf + read_bytes, 0, to_read);
        else {
            // release since this might take some time, so to make it more responsive
            rw_spinlock_release_read(&meta->access_lock);
            RESTORE_SIGNALS(sig);
            if (check_eintr())
                return read_bytes == 0 ? -EINTR : (ssize_t)read_bytes;

            ret = pread_file(sb->fd,
                buf + read_bytes, to_read,
                block * meta->block_size + offset % meta->block_size);
            if (ret < 0)
                return ret;
            if (ret != to_read)
                return -EIO;

            sig = PAUSE_SIGNALS();
            rw_spinlock_acquire_read(&meta->access_lock);
        }
        offset += to_read;
        read_bytes += to_read;
    }
    rw_spinlock_release_read(&meta->access_lock);
    ret = (ssize_t)read_bytes;

    end:
    RESTORE_SIGNALS(sig);
    return ret;
}

const struct vfs_ops ext2_op = {
    .fs_init = ext2_init,
    .fs_deinit = ext2_deinit,
    .lookup = ext2_lookup,
    .pread = ext2_pread,
    .readdir = ext2_readdir,

    .utimes_supported = 1,
    .chmod_supported  = 1,
    .chown_supported  = 1,
    .chgrp_supported  = 1,

    .btime_supported = 1,
    .mtime_supported = 1,
    .atime_supported = 1,

    .uid_max   = UINT32_MAX,
    .gid_max   = UINT32_MAX,

    .max_btime = INT32_MAX,
    .max_mtime = INT32_MAX,
    .max_atime = INT32_MAX,

    .min_btime = INT32_MIN,
    .min_mtime = INT32_MIN,
    .min_atime = INT32_MIN,
};