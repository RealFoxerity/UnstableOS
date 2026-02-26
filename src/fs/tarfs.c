#include "../include/fs/fs.h"
#include "../include/fs/vfs.h"
#include "../include/fs/tarfs.h"
#include "../../libc/src/include/ctype.h"
#include "../../libc/src/include/string.h"
#include "../include/mm/kernel_memory.h"
#include "../include/errno.h"

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
    .read = tarfs_read,
};

static inline size_t oct2int(const char * oct_data, size_t n) {
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        if(!isdigit(oct_data[i])) break;
        out *= 8;
        out += oct_data[i] - '0';
    }
    return out;
}

struct tar_node {
    char * path_fragment;
    char type;
    size_t size;        // extracted size
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

static struct tar_node * create_new_node(const char * path, const char * next_slash, size_t size, char type, off_t record_offset) {
    kassert(path);
    kassert(next_slash);

    struct tar_node * new;
    new = kalloc(sizeof(struct tar_node));
    kassert(new);
    memset(new, 0, sizeof(struct tar_node));

    *new = (struct tar_node) {
        .size = size,
        .type = type,
        .record_offset = record_offset,
    };
    new->path_fragment = kalloc(next_slash - path+1);
    kassert(new->path_fragment);
    memcpy(new->path_fragment, path, next_slash - path);
    new->path_fragment[next_slash - path] = 0;
    return new;
}

static char tar_add_cached_path(struct tar_node * root, const char * path, size_t size, char type, off_t record_offset) {
    kassert(root);
    kassert(path);

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

        if (checked->type == USTAR_SYMBOLIC_LINK || checked->type == USTAR_HARD_LINK) panic("We don't yet support links!\n");

        if (checked->type != USTAR_DIRECTORY) {
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
            kprintf("Registering %s\n", path);
            checked->inner = create_new_node(off, next_slash, size, type, record_offset);
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
            kprintf("Registering %s\n", path);
            checked->next = create_new_node(off, next_slash, size, type, record_offset);
            checked->next->upper = checked->upper;
            return 0;
        }
        checked = checked->next;
    }
}

static void tar_free_node(struct tar_node * node) {
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
    sb->data = create_new_node(root_path, root_path+1, 0, USTAR_DIRECTORY, 0);
    ((struct tar_node*)sb->data)->upper = sb->data;

    file_descriptor_t * tar_fd = sb->fd;

    off_t tarfs_size = seek_file(tar_fd, 0, SEEK_END);
    kprintf("tar fs size: %lu\n", tarfs_size);
    
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
        ssize_t curr_offset = seek_file(tar_fd, 0, SEEK_CUR);

        if (memcmp(hdr.ustar_magic, USTAR_MAGIC, sizeof(USTAR_MAGIC)-1) != 0 &&
            memcmp(hdr.ustar_magic, USTAR_MAGIC_ALT, sizeof(USTAR_MAGIC_ALT)-1) != 0
        ) {
            for (int i = 0; i < sizeof(ustar_hdr); i++) if (((char *)&hdr)[i] != 0) goto definitely_broken;

            nullblock_count ++;
            if (nullblock_count == 2) {
                if (curr_offset != tarfs_size - 1)
                    kprintf("Warning: Tar archive ends prematurely by %lu bytes!\n", tarfs_size - curr_offset);
                break;
            }
            continue;
            definitely_broken:
            kprintf("Tar archive corrupted/not in USTar format!\n");
            return -1;
        }
        memcpy(path, hdr.file_name, sizeof(hdr.file_name));
        memcpy(path + sizeof(hdr.file_name), hdr.filename_prefix, sizeof(hdr.filename_prefix));

        size_t file_length = oct2int(hdr.file_size, sizeof(hdr.file_size));
        size_t archive_length = file_length;
        if (file_length % 512 != 0) // all tar records are padded to 512 byte
            archive_length += 512 - (file_length%512);
        
        if (tar_add_cached_path(sb->data, path, file_length, hdr.type, curr_offset-512) != 0) {
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

inode_t * tarfs_lookup(superblock_t * sb, inode_t * last, const char * pathname) {
    kassert(sb);
    kassert(sb->data);
    kassert(pathname);

    size_t pathlen = strlen(pathname);

    struct tar_node * prev = last == NULL ? sb->data : last->id;
    struct tar_node * root = sb->data;
    struct tar_node * result = NULL;
    if (!is_valid_node(root, prev)) panic("Invalid checked/root TARFS node combo!");

    if (pathlen == 0 || (pathlen == 1 && pathname[0] == '.')) {             // current directory
        if (prev->type != USTAR_DIRECTORY) return VFS_LOOKUP_NOTDIRECTORY;
        result = prev;
    } else if (pathlen == 2 && strncmp("..", pathname, 2) == 0) {  // parent directory
        if (prev->type != USTAR_DIRECTORY) return VFS_LOOKUP_NOTDIRECTORY;
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
        if (result == NULL) return VFS_LOOKUP_NOTFOUND;
    }

    inode_t * ret = create_inode(sb, result);

    unsigned short mode; // not exactly safe, this could potentially ovewrite existing inode's mode
    mode = 
        (result->type == USTAR_DIRECTORY ? __ITMODE_DIR : 0) |
        (result->type == USTAR_CHAR_DEV ? __ITMODE_CHAR : 0) |
        (result->type == USTAR_BLOCK_DEV ? __ITMODE_BLK : 0);
    if (mode == 0) mode = __ITMODE_REG;

    inode_change_mode(ret, mode);

    return ret;
}

ssize_t tarfs_read(file_descriptor_t * fd, void * buf, size_t n) {
    if (n == 0) return 0;
    if (n > INT32_MAX) n = INT32_MAX;
    ssize_t read = 0;

    kassert(buf);
    kassert(fd);
    kassert(fd->inode->backing_superblock);

    superblock_t * sb = fd->inode->backing_superblock;
    file_descriptor_t * tar_fd = sb->fd;
    struct tar_node * root = sb->data;
    struct tar_node * this = fd->inode->id;

    if (!is_valid_node(root, this)) panic("Invalid this/root TARFS node combo!");

    if (fd->off >= this->size) return 0;
    if (fd->off + n >= this->size || fd->off + n < n) {
        n = this->size - fd->off;
    }

    spinlock_acquire_interruptible(&sb->lock); // so that we can't race for the file descriptor
    seek_file(tar_fd, this->record_offset + sizeof(ustar_hdr) + fd->off, SEEK_SET);
    read = read_file(tar_fd, buf, n); // read technically not needed here, but just in case

    spinlock_release(&sb->lock);

    if (read > 0) fd->off += read;
    return read;
}

off_t tarfs_seek(file_descriptor_t * fd, off_t off, int whence) {
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->id);

    struct tar_node * node = fd->inode->id;
    switch (whence) {
        case SEEK_SET:
            if (off < 0) return EINVAL;
            return fd->off = off;
        case SEEK_CUR:
            if (fd->off + off > fd->off && off < 0) return EINVAL; // underflow - negative offset
            if (fd->off + off < fd->off && off > 0) return E2BIG; // overflow

            return fd->off = fd->off + off;
        case SEEK_END:
            if (off >= 0) {
                if (fd->off + off > fd->off && off < 0) return EINVAL; // underflow - negative offset
                if (fd->off + off < fd->off && off > 0) return E2BIG; // overflow
                return fd->off = node->size + off;
            }
            else if (off < 0 && -off <= fd->off) return fd->off = fd->off - off;
            return EINVAL; // negative offset
        default:
            return EINVAL;
    }
}