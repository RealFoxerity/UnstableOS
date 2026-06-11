#include "fs/fs.h"
#include "fs/vfs.h"
#include "fs/tarfs.h"
#include <ctype.h>
#include <string.h>
#include "mm/kernel_memory.h"
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <stdint.h>

#include "dev_ops.h"

// TODO: among others, do header checksum, supposedly it's a sum of the header as an octal string
// TODO: this "driver" assumes that folders will have their entries before a file using them
// I am unsure whether this will always be the case and/or if it is standardised
// however all of the tar files I have tested have the directories as a separate entry before
// the file using them; maybe redo to have some form of reverse lookup?


const struct vfs_ops tar_op = {
    .fs_init = tar_load_fs,
    .fs_deinit = tar_unload_fs,
    .lookup = tarfs_lookup,
    .seek = tarfs_seek,
    .pread = tarfs_pread,
    .readdir = tarfs_readdir,
    .stat = tarfs_stat
};

static inline unsigned long long oct2int(const char * oct_data, size_t n) {
    unsigned long long out = 0;
    for (unsigned long long i = 0; i < n; i++) {
        if(!isdigit(oct_data[i])) break;
        out *= 8;
        out += oct_data[i] - '0';
    }
    return out;
}

static mode_t tar_mode_parse(const char * oct_data, size_t n) {
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        if(!isdigit(oct_data[i])) break;
        out *= 8;
        out += oct_data[i] - '0';
    }
    out &= __IPMODE_MASK;
    return out;
}

struct tar_node {
    char * path_fragment;
    uid_t uid;
    gid_t gid;
    dev_t device;
    mode_t mode;
    size_t size;        // extracted size
    time_t mtime;
    off_t record_offset; // offset to the header of this record
    struct tar_node * upper;
    struct tar_node * inner; // one level deeper, applies to directories
    struct tar_node * next; // next on this level
};


static inline char is_path_end(const char * next_slash) {
    return (
            *next_slash == '\0' ||
            (*next_slash == '/' && *(next_slash + 1) == '\0')
            );
}

static struct tar_node * create_new_node(const char * path, const char * next_slash, size_t size, uid_t uid, gid_t gid, dev_t dev, mode_t mode, time_t mtime, off_t record_offset) {
    kassert(path);
    kassert(next_slash);

    struct tar_node * new;
    new = kalloc(sizeof(struct tar_node));
    kassert(new);
    memset(new, 0, sizeof(struct tar_node));

    *new = (struct tar_node) {
        .size = size,
        .mode = mode,
        .record_offset = record_offset,
        .uid = uid,
        .gid = gid,
        .device = dev,
        .mtime = mtime
    };
    new->path_fragment = kalloc(next_slash - path+1);
    kassert(new->path_fragment);
    memcpy(new->path_fragment, path, next_slash - path);
    new->path_fragment[next_slash - path] = 0;
    return new;
}

static char tar_add_cached_path(struct tar_node * root, const char * path, ustar_hdr * hdr, size_t record_offset) {
    kassert(root);
    kassert(path);

    mode_t mode = 0;
    size_t size = oct2int(hdr->file_size, sizeof(hdr->file_size));
    uid_t uid = oct2int(hdr->owner_id, sizeof(hdr->owner_id));
    gid_t gid = oct2int(hdr->group_id, sizeof(hdr->group_id));
    dev_t dev = GET_DEV(
        oct2int(hdr->dev_major, sizeof(hdr->dev_major)),
        oct2int(hdr->dev_minor, sizeof(hdr->dev_minor))
    );
    time_t mtime = oct2int(hdr->last_modified_time, sizeof(hdr->last_modified_time));

    switch (hdr->type) {
        case USTAR_NORMAL_ALT:
        case USTAR_NORMAL:
            mode = S_IFREG;
            break;
        case USTAR_DIRECTORY:
            mode = S_IFDIR;
            break;
        case USTAR_BLOCK_DEV:
            mode = S_IFBLK;
            break;
        case USTAR_HARD_LINK:
        case USTAR_SYMBOLIC_LINK:
        case USTAR_CHAR_DEV:
        case USTAR_PIPE:
            kprintf("We don't support links, fifos, pipes, and char devs: %d", mode);
            return -1;
    }

    mode |= tar_mode_parse(hdr->modes, sizeof(hdr->modes));
    struct tar_node * checked = root;

    const char * off = path;
    if (*off == '/') off ++;

    const char * next_slash = strchrnul(off, '/');
    if (checked->inner == NULL) { // the virtual directory / is handled a bit differently, we need to have it though to create the initial structure
        if (!is_path_end(next_slash)) {
            kprintf("No root subdirectories cached, yet tried to add a compound path %s!\n", path);
            return -1;
        }
        goto init_folder;
    }

    checked = checked->inner;

    while (1) {
        kassert(checked->path_fragment);
        kassert(checked->upper);
        // this could be 100% done without gotos, but i think it looks better like this
        if (strlen(checked->path_fragment) != next_slash - off) goto next;
        if (memcmp(off, checked->path_fragment, next_slash - off) != 0) goto next;

        if (is_path_end(next_slash)) {
            kprintf("Path %s already exists!\n", path);
            return -1;
        }

        if (!S_ISDIR(checked->mode)) {
            kprintf("Path fragment %s of %s would map to a non-directory entry!\n", off, path);
            return -1;
        }

        off = next_slash + 1;
        next_slash = strchrnul(off, '/');

        if (checked->inner == NULL) { // directory exists, but is empty
            if (!is_path_end(next_slash)) {
                kprintf("Path fragment %s of %s requires non-existent subdirectory!\n", off, path);
                return -1;
            }
            init_folder:
            //kprintf("Registering mode %hx path %s\n", mode, path);
            checked->inner = create_new_node(off, next_slash, size, uid, gid, dev, mode, mtime, record_offset);
            checked->inner->upper = checked;
            return 0;
        }
        checked = checked->inner;
        continue;

        next:
        if (checked->next == NULL) {
            if (!is_path_end(next_slash)) {
                kprintf("Path fragment %s of %s specifies non-existent directory!\n", off, path);
                return -1;
            }
            //kprintf("Registering mode %hx path %s\n", mode, path);
            checked->next = create_new_node(off, next_slash, size, uid, gid, dev, mode, mtime, record_offset);
            checked->next->upper = checked->upper;
            return 0;
        }
        checked = checked->next;
    }
}

static void tar_free_node(struct tar_node * node) {
    if (node == NULL) return;
    if (node->inner != NULL) {
        tar_free_node(node->inner);
    }
    if (node->next != NULL) {
        tar_free_node(node->next);
    }
    kfree(node);
}

int tar_load_fs(superblock_t * sb) {
    const char * root_path = "/";
    sb->data = create_new_node(root_path, root_path+1, 0, 0, 0, 0, S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, 0, 0);
    ((struct tar_node*)sb->data)->upper = sb->data;

    file_descriptor_t * tar_fd = sb->fd;

    off_t tarfs_size = seek_file(tar_fd, 0, SEEK_END);
    kprintf("tar fs size: %llu\n", tarfs_size);

    if (tarfs_size < sizeof(ustar_hdr)) {
        kprintf("Tar archive truncated!\n");
        return -1;
    }

    ssize_t read_bytes = 0;
    ustar_hdr hdr = {0};
    seek_file(tar_fd, 0, SEEK_SET);

    char nullblock_count = 0;

    char * path = kalloc(MAX_PATH_TAR + 1); // +1 for the null byte so we don't accidentally overread
    kassert(path);
    memset(path, 0, MAX_PATH_TAR + 1);

    while ((read_bytes = read_file(tar_fd, &hdr, sizeof(ustar_hdr))) == sizeof(ustar_hdr)) {
        off_t curr_offset = seek_file(tar_fd, 0, SEEK_CUR);

        if (memcmp(hdr.ustar_magic, USTAR_MAGIC, sizeof(USTAR_MAGIC)-1) != 0 &&
            memcmp(hdr.ustar_magic, USTAR_MAGIC_ALT, sizeof(USTAR_MAGIC_ALT)-1) != 0
        ) {
            for (int i = 0; i < sizeof(ustar_hdr); i++) if (((char *)&hdr)[i] != 0) goto definitely_broken;

            nullblock_count ++;
            if (nullblock_count == 2) {
                if (curr_offset != tarfs_size)
                    kprintf("Info: %llu bytes of unused padding after USTAR initramfs\n", tarfs_size - curr_offset);
                break;
            }
            continue;
            definitely_broken:
            kfree(path);
            tar_free_node(sb->data);
            kprintf("Tar archive corrupted/not in USTAR format!\n");
            return -1;
        }
        memcpy(path, hdr.file_name, sizeof(hdr.file_name));
        memcpy(path + sizeof(hdr.file_name), hdr.filename_prefix, sizeof(hdr.filename_prefix));
        size_t file_length = oct2int(hdr.file_size, sizeof(hdr.file_size));
        if (curr_offset + file_length > tarfs_size) {
            kprintf("Warning: USTAR entry %s truncated by %llu bytes, skipping remaining!\n",
                path, curr_offset + file_length - tarfs_size);
            break;
        }
        size_t archive_length = file_length;
        if (file_length % 512 != 0) // all tar records are padded to 512 byte
            archive_length += 512 - (file_length%512);

        if (tar_add_cached_path(sb->data, path, &hdr, curr_offset-512) != 0) {
            kprintf("Failed to build internal tar structures - failed on path %s!\n", path);
            tar_free_node(sb->data);
            return -1;
        }

        seek_file(tar_fd, archive_length, SEEK_CUR);
    }
    return 0;
}

int tar_unload_fs(superblock_t * sb) {
    tar_free_node(sb->data);
    return 0;
}

// sanity check that the pointer and fs structure is valid
static char is_valid_node(const struct tar_node * root, const struct tar_node * curr) {
    if (root->upper != root) panic("Unlinked root TARFS node!");

    const struct tar_node * temp = curr;
    while (temp != root) {
        if (temp->upper == NULL) panic("Tried using unlinked TARFS node!");
        if (temp->path_fragment == NULL) panic("Tried using anonymous TARFS node!");

        if (temp->upper == temp) return 0; // root is not the correct one for curr
        temp = temp->upper;
    }

    return 1;
}

long tarfs_lookup(superblock_t * sb, inode_t * last, const char * pathname, inode_t ** inode_out) {
    if (!pathname) return -EFAULT;
    if (!sb) return -EFAULT;
    if (!inode_out) return -EFAULT;

    kassert(sb->data);

    size_t pathlen = strlen(pathname);

    struct tar_node * prev = last == NULL ? sb->data : last->id;

    if (!S_ISDIR(prev->mode)) return -ENOTDIR;

    struct tar_node * root = sb->data;
    struct tar_node * result = NULL;
    if (!is_valid_node(root, prev)) panic("Invalid checked/root TARFS node combo!");

    if (pathlen == 0 || (pathlen == 1 && pathname[0] == '.')) {             // current directory
        result = prev;
    } else if (strcmp("..", pathname) == 0) {  // parent directory
        if (prev == root) return VFS_LOOKUP_ESCAPE;
        result = prev->upper;
    } else {
        for (
            struct tar_node * checked = prev->inner;
            checked != NULL;
            checked = checked->next
        ) {
            if (checked->path_fragment == NULL)
                panic("Anonymous TARFS node encountered!");
            if (strlen(checked->path_fragment) != pathlen)
                continue;

            if (strncmp(checked->path_fragment, pathname, pathlen) == 0) {
                result = checked;
                break;
            }
        }
        if (result == NULL) return -ENOENT;
    }

    inode_t new_inode = {
        .id = result,
        .backing_superblock = sb,
        .mode = result->mode,
        .size = result->size,
    };

    if (S_ISCHR(result->mode) || S_ISBLK(result->mode)) {
        new_inode.device = result->device;
    } /* else if (S_ISFIFO(result->mode)) {
        new_inode.pipe = ????
    }*/

    long status = register_inode(&new_inode, inode_out);

    return status;
}

ssize_t tarfs_pread(file_descriptor_t * fd, void * buf, size_t n, off_t offset) {
    if (offset < 0) return -EINVAL;

    if (n == 0) return 0;
#ifdef E2BIG_ON_2G
    if (n > SSIZE_MAX) return -E2BIG;
#else
    if (n > SSIZE_MAX) n = SSIZE_MAX;
#endif
    ssize_t read = 0;

    kassert(buf);
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->id);
    kassert(fd->inode->backing_superblock);

    superblock_t * sb = fd->inode->backing_superblock;
    file_descriptor_t * tar_fd = sb->fd;
    struct tar_node * root = sb->data;
    struct tar_node * this = fd->inode->id;

    if (!S_ISREG(this->mode)) return -EINVAL;

    if (!is_valid_node(root, this)) panic("Invalid this/root TARFS node combo!");

    if (offset >= this->size) return 0;
    if (offset + n >= this->size || offset + n < n) {
        n = this->size - offset;
    }

    spinlock_acquire_interruptible(&sb->lock); // so that we can't race for the file descriptor
    kassert(sb->is_mounted);
    read = pread_file(tar_fd, buf, n, this->record_offset + sizeof(ustar_hdr) + offset);

    spinlock_release(&sb->lock);

    return read;
}

off_t tarfs_seek(file_descriptor_t * fd, off_t off, int whence) {
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->id);

    struct tar_node * node = fd->inode->id;
    return generic_seek(fd, off, whence, node->size);
}

ssize_t tarfs_readdir(file_descriptor_t * fd, struct dirent * dent, size_t dent_size, off_t offset) {
    kassert(dent);

    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->id);
    kassert(fd->inode->backing_superblock);

    superblock_t * sb = fd->inode->backing_superblock;
    struct tar_node * root = sb->data;
    struct tar_node * this = fd->inode->id;

    if (!is_valid_node(root, this)) panic("Invalid this/root TARFS node combo!");

    switch (offset) {
        case 0: // "."
            if (dent_size < sizeof(struct dirent) + 2)
                return -EINVAL;
            *dent = (struct dirent) {
                .d_ino = this->record_offset,
                .d_off = offset,
                .d_reclen = sizeof(struct dirent) + 2,
                .d_type = DT_DIR,
            };
            dent->d_name[0] = '.';
            dent->d_name[1] = '\0';
            break;
        case 1:
            if (dent_size < sizeof(struct dirent) + 3)
                return -EINVAL;
            *dent = (struct dirent) {
                .d_ino = this->upper->record_offset,
                .d_off = offset,
                .d_reclen = sizeof(struct dirent) + 3,
                .d_type = DT_DIR,
            };
            memcpy(dent->d_name, "..", 3);
            break;
        default:
            this = this->inner;
            if (this == NULL) return 0; // empty directory
            for (int i = 0; i < offset - 2; i++) {
                this = this->next;
                if (this == NULL) return 0;
            }
            if (dent_size < sizeof(struct dirent) + strlen(this->path_fragment) + 1)
                return -EINVAL;
            *dent = (struct dirent) {
                .d_ino = this->record_offset,
                .d_off = offset,
                .d_reclen = sizeof(struct dirent) + strlen(this->path_fragment) + 1,
                .d_type = IFTODT(this->mode)
            };
            memcpy(&dent->d_name, this->path_fragment, strlen(this->path_fragment) + 1);
    }
    offset++;
    __atomic_store_n(&fd->off, offset, __ATOMIC_RELAXED);
    return dent->d_reclen;
}


int tarfs_stat(inode_t * file, struct stat * buf) {
    kassert(file->id);
    kassert(file->backing_superblock);
    kassert(file->backing_superblock->data);

    superblock_t * sb = file->backing_superblock;

    struct tar_node * root = sb->data;
    struct tar_node * this = file->id;

    if (!is_valid_node(root, this)) panic("Invalid this/root TARFS node combo!");

    if (this->record_offset > INT32_MAX || this->size > INT32_MAX) return -EOVERFLOW;

    *buf = (struct stat) {
        .st_dev = sb->device,
        .st_ino = this->record_offset,
        .st_mode = this->mode,
        .st_size = this->size,
        .st_uid = this->uid,
        .st_gid = this->gid,
        .st_rdev = this->device,
        .st_mtime = this->mtime,
        .st_nlink = 1, // not correct, but whatevs
        .st_blksize = sizeof(ustar_hdr),
        .st_blocks = (this->size + sizeof(ustar_hdr) - 1) / sizeof(ustar_hdr),
    };
    return 0;
}