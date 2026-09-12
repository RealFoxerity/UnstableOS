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

// TODO: add fragment checking if/when adding fragment support
// note: neither Linux, BSD, or HURD support fragments :p
    static int ext2_get_inode(const superblock_t * sb, ino_t ino, struct ext2_inode * out);

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

    if (!(sb->mount_options & MOUNT_RDONLY))
        ext2_replenish_cache(sb, 0);
    return 0;
}

int ext2_deinit(superblock_t * sb) {
    kassert(sb);
    struct ext2_metadata * meta = sb->data;
    if (!(sb->mount_options & MOUNT_RDONLY))
        if (pwrite_file(sb->fd, &meta->sb, sizeof(struct ext2_sb), 1024) < 0)
            dkprintf("Warning: Failed superblock write\n");
    kfree(sb->data);
    return 0;
}

// lock if looking up, lockless if inode already exists
static off_t ext2_get_inode_offset(const superblock_t * sb, ino_t ino) {
    kassert(ino > 0);
    kassert(sb);

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

    return bgd.inode_table_block * meta->block_size + block_index * meta->sb.inode_size;
}

static int ext2_get_inode(const superblock_t * sb, ino_t ino, struct ext2_inode * out) {
    kassert(ino > 0);
    kassert(sb);
    kassert(out);

    off_t inode_off = ext2_get_inode_offset(sb, ino);
    if (inode_off < 0)
        return (int)inode_off;

    if (pread_file(sb->fd,
        out, sizeof(struct ext2_inode),
        inode_off) != sizeof(struct ext2_inode))
            return -EIO;
    return 0;
}

static int ext2_lookup_internal(superblock_t * sb, struct ext2_inode * ino, const char * pathname, struct ext2_directory * dirent) {
    kassert(sb);
    kassert(sb->data);

    struct ext2_metadata * meta = sb->data;

    char name[256] = {0};
    for (size_t i = 0; ; i++) {
        unsigned long block = ext2_get_block(sb, ino, i, 1, 0);
        if (block == 0)
            return -ENOENT;
        for (size_t offset = 0; offset < meta->block_size;) {
            if (pread_file(sb->fd,
                dirent, sizeof(struct ext2_directory),
                block * meta->block_size + offset) != sizeof(struct ext2_directory) ||
                dirent->name_len > dirent->rec_len - 8)
                    return -EIO;
            if (pread_file(sb->fd,
                name, dirent->name_len,
                block * meta->block_size + offset + sizeof(struct ext2_directory)
                ) != dirent->name_len)
                    return -EIO;

            name[dirent->name_len] = '\0';
            if (dirent->inode != 0 && strncmp(pathname, name, 255) == 0)
                return 0;
            if (dirent->rec_len == 0) {
                dkprintf("Warning: directory entry with record length 0, skipping rest of block %lu\n", block);
                break;
            }
            offset += dirent->rec_len;
            if (offset % 4)
                dkprintf("Warning: Unaligned directory entry at block %lu\n", block);
        }
    }
    return -ENOENT;
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
    rw_spinlock_acquire_read(&meta->access_lock);
    ret = ext2_lookup_internal(sb, &ino, pathname, &dirent);
    if (ret < 0) {
        rw_spinlock_release_read(&meta->access_lock);
        goto end;
    }

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
            .block_count = ino.used_512blocks,
        };
        if (S_ISBLK(new_inode.mode) || S_ISCHR(new_inode.mode))
            new_inode.device = ino.blocks[0] ? ino.blocks[0] : ino.blocks[1];
        new_inode.device &= 0x7FFF;
        // we don't have a true separation between char and block devices like ext2 expects
        if (S_ISCHR(new_inode.mode))
            new_inode.device |= 0x8000;

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
    unsigned long block = ext2_get_block(sb, &ino, block_number, 1, 0);
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
        block = ext2_get_block(sb, &ino, block_number, 1, 0);
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
    while (read_bytes < n && offset < fd->inode->size) {
        unsigned long block       = ext2_get_block(sb, &ino, offset, 0, 0);
        size_t remaining_of_block = meta->block_size - offset % meta->block_size;
        size_t to_read            = remaining_of_block > n - read_bytes ? n - read_bytes : remaining_of_block;
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

ssize_t ext2_pwrite(file_descriptor_t * fd, const void * buf, size_t n, off_t offset) {
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
    ssize_t ret;
    off_t inode_offset = ext2_get_inode_offset(sb, fd->inode->id);
    if (inode_offset < 0) {
        ret = (ssize_t)inode_offset;
        goto end;
    }
    ret = ext2_get_inode(sb, fd->inode->id, &ino);
    if (ret < 0)
        goto end;

    rw_spinlock_acquire_write(&meta->access_lock);
    size_t written_bytes = 0;
    while (written_bytes < n) {
        unsigned long block = ext2_get_block(sb, &ino, offset, 0, 1);
        if (block) {
            if (pwrite_file(sb->fd,
                &ino, sizeof(ino),
                inode_offset) < 0
            ) {
                ret = -EIO;
                rw_spinlock_release_write(&meta->access_lock);
                goto end;
            }
            fd->inode->block_count = ino.used_512blocks;
        }
        size_t remaining_of_block = meta->block_size - offset % meta->block_size;
        size_t to_write           = remaining_of_block > n - written_bytes ? n - written_bytes : remaining_of_block;

        // release since this might take some time, so to make it more responsive
        rw_spinlock_release_write(&meta->access_lock);
        RESTORE_SIGNALS(sig);
        if (!block) {
            ret = -ENOSPC;
            goto end2;
        }

        if (check_eintr()) {
            eintr:
            ret = written_bytes == 0 ? -EINTR : (ssize_t)written_bytes;
            goto end2;
        }

        ret = pwrite_file(sb->fd,
            buf + written_bytes, to_write,
            block * meta->block_size + offset % meta->block_size);
        if (ret == -EINTR)
            goto eintr;

        if (ret < 0)
            goto end2;

        written_bytes += ret;
        offset += ret;

        if (ret != to_write) {
            ret = (ssize_t)written_bytes;
            goto end2;
        }

        sig = PAUSE_SIGNALS();
        rw_spinlock_acquire_write(&meta->access_lock);
    }
    rw_spinlock_release_write(&meta->access_lock);
    ret = (ssize_t)written_bytes;

    end:
    RESTORE_SIGNALS(sig);
    end2:
    spinlock_acquire(&fd->inode->lock);
    if (offset > fd->inode->size)
        fd->inode->size = offset;
    spinlock_release(&fd->inode->lock);
    return ret;
}

static int ext2_trunc_to_size(superblock_t * sb, struct ext2_inode * ino, unsigned long blocks) {
    kassert(sb);
    kassert(sb->data);
    kassert(ino);

    struct ext2_metadata * meta = sb->data;
    if (blocks <= 12 + meta->block_size / 4 + (meta->block_size / 4) * (meta->block_size / 4)) {
        int ret = ext2_free_triply_indirect(sb, ino->t_indirect_block, 0);
        if (ret < 0)
            return ret;

        ext2_free(sb, ino->t_indirect_block, 0);
        ino->t_indirect_block = 0;
    } else {
        return ext2_free_triply_indirect(sb, ino->t_indirect_block,
            blocks - 12 - meta->block_size / 4 - (meta->block_size / 4) * (meta->block_size / 4));
    }
    if (blocks <= 12 + meta->block_size / 4) {
        int ret = ext2_free_doubly_indirect(sb, ino->d_indirect_block, 0);
        if (ret < 0)
            return ret;
        ext2_free(sb, ino->d_indirect_block, 0);
        ino->d_indirect_block = 0;
    } else {
        return ext2_free_doubly_indirect(sb, ino->t_indirect_block,
            blocks - 12 - meta->block_size / 4);
    }
    if (blocks <= 12) {
        int ret = ext2_free_indirect(sb, ino->indirect_block, 0);
        if (ret < 0)
            return ret;
        ext2_free(sb, ino->indirect_block, 0);
        ino->indirect_block = 0;
    } else {
        return ext2_free_indirect(sb, ino->indirect_block,
            blocks - 12);
    }

    for (unsigned long i = blocks; i < 12; i++) {
        ext2_free(sb, ino->blocks[i], 0);
        ino->blocks[i] = 0;
    }
    return 0;
}

int ext2_trunc(inode_t * file, off_t length) {
    kassert(file);
    if (length < 0)
        return -EINVAL;
    if (!S_ISREG(file->mode))
        return -EINVAL;

    superblock_t * sb = file->backing_superblock;
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    spinlock_acquire(&file->lock);
    if (length > file->size) {
        file->size = length; // ext2 supports sparse files
        spinlock_release(&file->lock);
        return 0;
    }
    spinlock_release(&file->lock);

    if (check_eintr())
        return -EINTR;

    sigset_t sig = PAUSE_SIGNALS();
    rw_spinlock_acquire_write(&meta->access_lock);
    int ret;
    unsigned long target_blocks = (length + meta->block_size - 1) / meta->block_size;
    struct ext2_inode ino;
    off_t inode_offset = ext2_get_inode_offset(sb, file->id);
    if (inode_offset < 0) {
        ret = (int)inode_offset;
        goto err;
    }
    ret = ext2_get_inode(sb, file->id, &ino);
    if (ret < 0)
        goto err;

    ret = ext2_trunc_to_size(sb, &ino, target_blocks);
    if (ret < 0)
        goto err;
    ino.used_512blocks = target_blocks * (meta->block_size / 512);
    ret = pwrite_file(sb->fd,
        &ino, sizeof(ino),
        inode_offset);
    if (ret < 0)
        goto err;

    spinlock_acquire(&file->lock);
    file->size = length;
    file->block_count = ino.used_512blocks;
    spinlock_release(&file->lock);

    ret = 0;

    err:
    rw_spinlock_release_write(&meta->access_lock);
    RESTORE_SIGNALS(sig);
    return ret;
}

int ext2_release(inode_t * inode) {
    kassert(inode);
    kassert(inode->backing_superblock);
    superblock_t * sb = inode->backing_superblock;
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    sigset_t sig = PAUSE_SIGNALS();
    struct ext2_inode ino;
    int ret = ext2_get_inode(sb, inode->id, &ino);
    if (ret < 0)
        goto end;

    off_t offset = ext2_get_inode_offset(sb, inode->id);
    if (offset < 0) {
        ret = (int)offset;
        goto end;
    }

    rw_spinlock_acquire_write(&meta->access_lock);

    if (inode->nlink == 0) {
        ext2_trunc_to_size(sb, &ino, 0);
        ext2_free(sb, inode->id, 1);
        goto freed;
    }

    ino.mode = inode->mode;
    ino.uid  = inode->uid;
    ino.osd2_linux.uid_high = inode->uid >> 16;
    if (meta->large_files) {
        ino.lo_size = inode->size;
        ino.hi_size = (int32_t)(inode->size >> 32);
    } else
        ino.size = (int32_t)inode->size;
    ino.atime = (int32_t)inode->atime;
    ino.btime = (int32_t)inode->btime;
    ino.mtime = (int32_t)inode->mtime;

    ino.gid = inode->gid;
    ino.osd2_linux.gid_high = inode->gid >> 16;

    ino.nlink = inode->nlink;
    //ino.used_512blocks = inode->block_count;


    ret = pwrite_file(sb->fd,
        &ino, sizeof(ino),
        offset);

    freed:
    rw_spinlock_release_write(&meta->access_lock);
    end:
    RESTORE_SIGNALS(sig);
    return ret;
}

static void ext2_inodet_to_ext2inode(const inode_t * inode, struct ext2_inode * out) {
    kassert(inode);
    kassert(out);
    memset(out, 0, sizeof(struct ext2_inode));

    out->mode = inode->mode;
    out->uid  = inode->uid;
    out->atime = (int32_t)inode->atime;
    out->btime = (int32_t)inode->btime;
    out->mtime = (int32_t)inode->mtime;
    out->gid = inode->gid;
    out->nlink = inode->nlink;
    out->osd2_linux.uid_high = inode->uid >> 16;
    out->osd2_linux.gid_high = inode->gid >> 16;

    if (S_ISBLK(inode->mode) || S_ISCHR(inode->mode))
        out->blocks[0] = inode->device & 0x7FFF;
}

static off_t ext2_creat_internal(superblock_t * sb, struct ext2_inode * ino, const struct ext2_inode * new, ino_t parent_inode, const char * pathname) {
    struct ext2_directory dent;
    int ret = ext2_lookup_internal(sb, ino, pathname, &dent);
    if (ret == 0)
        return -EEXIST;
    if (ret != -ENOENT)
        return ret;

    unsigned long new_ino = ext2_allocate(sb, parent_inode, 1);
    if (new_ino == 0)
        return -ENOSPC;
    ret = ext2_alloc_dentry(sb, ino, pathname, new_ino, new->mode);
    if (ret < 0) {
        ext2_free(sb, new_ino, 1);
        return ret;
    }
    off_t inode_offset = ext2_get_inode_offset(sb, new_ino);
    if (inode_offset < 0) {
        dkprintf("Warning: error on inode write, dangling directory entry!\n");
        ext2_free(sb, new_ino, 1);
        return inode_offset;
    }

    ret = pwrite_file(sb->fd,
        new, sizeof(struct ext2_inode),
        inode_offset);
    if (ret < 0) {
        dkprintf("Warning: error on inode write, dangling directory entry!\n");
        ext2_free(sb, new_ino, 1);
        return ret;
    }
    return new_ino;
}

int ext2_creat(inode_t * parent, const char * pathname, mode_t mode, inode_t ** inode_out) {
    if (strcmp(pathname, ".") == 0 || strcmp(pathname, "..") == 0)
        return -EINVAL;
    mode &= ~S_IFMT;
    kassert(parent);
    if (!S_ISDIR(parent->mode))
        return -ENOTDIR;

    kassert(pathname);
    kassert(inode_out);
    superblock_t * sb = parent->backing_superblock;
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    inode_t new = {
        .uid = parent->mode & S_ISUID ? parent->uid : current_process->euid,
        .gid = parent->mode & S_ISGID ? parent->gid : current_process->egid,
        .nlink = 1,
        .atime = system_time_sec,
        .ctime = system_time_sec,
        .mtime = system_time_sec,
        .btime = system_time_sec,
        .backing_superblock = sb,
        .mode = mode | S_IFREG,
        .io_block_size = (blksize_t)meta->block_size,
    };

    struct ext2_inode e2new, ino;
    ext2_inodet_to_ext2inode(&new, &e2new);

    if (check_eintr())
        return -EINTR;
    sigset_t sig = PAUSE_SIGNALS();

    int ret = ext2_get_inode(sb, parent->id, &ino);
    if (ret < 0) {
        RESTORE_SIGNALS(sig);
        return ret;
    }
    rw_spinlock_acquire_write(&meta->access_lock);

    off_t new_inode = ext2_creat_internal(sb, &ino, &e2new, parent->id, pathname);
    if (new_inode < 0)
        ret = (int)new_inode;
    else
        new.id = new_inode;

    rw_spinlock_release_write(&meta->access_lock);
    RESTORE_SIGNALS(sig);
    if (ret == 0)
        return register_inode(&new, inode_out, 0);
    return ret;
}

int ext2_mkdir(inode_t * parent, const char * pathname, mode_t mode, inode_t ** inode_out) {
    if (strcmp(pathname, ".") == 0 || strcmp(pathname, "..") == 0)
        return -EINVAL;
    mode &= ~S_IFMT;
    kassert(parent);
    if (!S_ISDIR(parent->mode))
        return -ENOTDIR;

    kassert(pathname);
    kassert(inode_out);
    superblock_t * sb = parent->backing_superblock;
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    inode_t new = {
        .uid = parent->mode & S_ISUID ? parent->uid : current_process->euid,
        .gid = parent->mode & S_ISGID ? parent->gid : current_process->egid,
        .nlink = 2,
        .size = meta->block_size,
        .atime = system_time_sec,
        .ctime = system_time_sec,
        .mtime = system_time_sec,
        .btime = system_time_sec,
        .backing_superblock = sb,
        .mode = mode | S_IFDIR,
        .io_block_size = (blksize_t)meta->block_size,
        .block_count = meta->block_size / 512,
    };

    struct ext2_inode e2new, ino;
    ext2_inodet_to_ext2inode(&new, &e2new);

    if (check_eintr())
        return -EINTR;
    sigset_t sig = PAUSE_SIGNALS();

    int ret = ext2_get_inode(sb, parent->id, &ino);
    if (ret < 0) {
        RESTORE_SIGNALS(sig);
        return ret;
    }
    rw_spinlock_acquire_write(&meta->access_lock);

    unsigned long dir_block = ext2_allocate(sb, ino.blocks[0], 0);
    if (dir_block == 0) {
        ret = -ENOSPC;
        goto err;
    }

    e2new.blocks[0] = dir_block;
    e2new.size = (int32_t)meta->block_size;
    e2new.used_512blocks = meta->block_size / 512;

    off_t new_inode = ext2_creat_internal(sb, &ino, &e2new, parent->id, pathname);
    if (new_inode < 0) {
        ret = (int)new_inode;
        ext2_free(sb, dir_block, 0);
        goto err;
    }

    new.id = new_inode;

    unsigned char new_dir_buffer[24] = {0};
    *(struct ext2_directory*)new_dir_buffer =
        (struct ext2_directory) {
            .inode = new_inode,
            .rec_len = 12,
            .name_len = 1,
            .file_type = meta->filetype ? EXT2_FT_DIR : 0,
        };
    new_dir_buffer[sizeof(struct ext2_directory)] = '.';

    *(struct ext2_directory*)(new_dir_buffer + 12) =
        (struct ext2_directory) {
            .inode = parent->id,
            .rec_len = meta->block_size - 12,
            .name_len = 2,
            .file_type = meta->filetype ? EXT2_FT_DIR : 0,
        };
    new_dir_buffer[12 + sizeof(struct ext2_directory) + 0] = '.';
    new_dir_buffer[12 + sizeof(struct ext2_directory) + 1] = '.';

    ret = pwrite_file(sb->fd,
        new_dir_buffer, sizeof(new_dir_buffer),
        dir_block * meta->block_size);
    if (ret < 0) {
        dangle:
        dkprintf("Warning: error on mkdir write, dangling directory entry!\n");
        ext2_free(sb, new_inode, 1);
        ext2_free(sb, dir_block, 0);
        goto err;
    }

    off_t inode_offset = ext2_get_inode_offset(sb, new_inode);
    if (inode_offset < 0) {
        ret = (int)inode_offset;
        goto dangle;
    }
    ret = pwrite_file(sb->fd,
        &e2new, sizeof(e2new),
        inode_offset);
    if (ret < 0)
        goto dangle;
    ret = 0;

    parent->nlink++;
    ext2_adjust_bgroup_dir_count(sb, parent->id, 1);

    err:
    rw_spinlock_release_write(&meta->access_lock);
    RESTORE_SIGNALS(sig);
    if (ret == 0)
        return register_inode(&new, inode_out, 0);
    return ret;
}

int ext2_mknod(inode_t * parent, const char * pathname, mode_t mode, dev_t dev) {
    if (strcmp(pathname, ".") == 0 || strcmp(pathname, "..") == 0)
        return -EINVAL;
    if (!S_ISBLK(mode) && !S_ISCHR(mode))
        return -EINVAL;
    kassert(parent);
    if (!S_ISDIR(parent->mode))
        return -ENOTDIR;

    kassert(pathname);
    superblock_t * sb = parent->backing_superblock;
    kassert(sb);
    kassert(sb->data);
    struct ext2_metadata * meta = sb->data;

    inode_t new = {
        .uid = parent->mode & S_ISUID ? parent->uid : current_process->euid,
        .gid = parent->mode & S_ISGID ? parent->gid : current_process->egid,
        .nlink = 1,
        .atime = system_time_sec,
        .ctime = system_time_sec,
        .mtime = system_time_sec,
        .btime = system_time_sec,
        .backing_superblock = sb,
        .mode = mode,
        .io_block_size = (blksize_t)meta->block_size,
        .device = dev,
    };

    struct ext2_inode e2new, ino;
    ext2_inodet_to_ext2inode(&new, &e2new);

    if (check_eintr())
        return -EINTR;
    sigset_t sig = PAUSE_SIGNALS();

    int ret = ext2_get_inode(sb, parent->id, &ino);
    if (ret < 0) {
        RESTORE_SIGNALS(sig);
        return ret;
    }
    rw_spinlock_acquire_write(&meta->access_lock);

    off_t new_inode = ext2_creat_internal(sb, &ino, &e2new, parent->id, pathname);
    if (new_inode < 0)
        ret = (int)new_inode;
    else
        ret = 0;

    rw_spinlock_release_write(&meta->access_lock);
    RESTORE_SIGNALS(sig);
    return ret;
}

const struct vfs_ops ext2_op = {
    .fs_init = ext2_init,
    .fs_deinit = ext2_deinit,
    .lookup = ext2_lookup,
    .release = ext2_release,
    .pread = ext2_pread,
    .pwrite = ext2_pwrite,
    .trunc = ext2_trunc,
    .creat = ext2_creat,
    .mkdir = ext2_mkdir,
    .mknod = ext2_mknod,

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