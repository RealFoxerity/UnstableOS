#include "../include/fs/fs.h"
#include "../include/fs/vfs.h"
#include "../include/kernel_sched.h"
#include "../include/errno.h"
#include "../include/mm/kernel_memory.h"

#include "../../libc/src/include/string.h"
#include "../../libc/src/include/sys/limits.h"
#include "../include/kernel_tty_io.h"
#include "../include/block/memdisk.h"
#include "../../libc/src/include/fcntl.h"


static int __open_raw_device(dev_t device, unsigned short flags, file_descriptor_t ** file_out) {
    kassert(file_out);
    file_descriptor_t * file = get_free_fd();
    if (!file) {
        return -ENFILE;
    }

    inode_t * dev_inode = inode_from_device(device);
    kassert(dev_inode);

    file->inode = dev_inode;
    file->flags = flags;
    *file_out = file;
    return 0;
}
int open_raw_device(dev_t device, unsigned short flags, file_descriptor_t ** file_out) {
    if (flags == 0 || flags > O_RDWR) return -EINVAL;
    int ret = 0;
    spinlock_acquire(&kernel_fd_lock);

    ret = __open_raw_device(device, flags, file_out);

    spinlock_release(&kernel_fd_lock);
    return ret;
}
int open_raw_device_fd(dev_t device, unsigned short flags) {
    if (flags == 0 || flags > O_RDWR) return -EINVAL;

    spinlock_acquire(&kernel_fd_lock);

    int fd = -1;
    for (int i = 0; i < FD_LIMIT_PROCESS; i++) {
        if (!current_process->fds[i]) {
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        spinlock_release(&kernel_fd_lock);
        return -EMFILE;
    }

    file_descriptor_t * file = NULL;
    int ret = __open_raw_device(device, flags, &file);
    if (ret < 0 || file == NULL) {
        spinlock_release(&kernel_fd_lock);
        return ret;
    }
    current_process->fds[fd] = file;

    spinlock_release(&kernel_fd_lock);

    return fd;
}

static char check_page(const char * addr) {
    if ((PAGE_DIRECTORY_TYPE*)addr >= PTE_ADDR_VIRT_BASE) return 0; // colliding with page tables
    if (addr < (const char *)LOWEST_PHYS_ADDR_ALLOWABLE) return 0; // we don't store data in low memory

    PAGE_TABLE_TYPE * pte = paging_get_pte(addr);
    if (pte == NULL) return 0;
    if (*pte == 0) return 0; // not present
    if (current_process->pid != 0 && !(*pte & PTE_PDE_PAGE_USER_ACCESS)) return 0; // userspace doesn't have access

    return 1;
}

static inline char * secure_strdup(const char * path, size_t max_len, size_t *len_out) {
    if (path == NULL) return NULL;

    size_t path_len;
    for (path_len = 0; path_len < max_len; path_len++) {
        if (!check_page(path + path_len)) return NULL;
        if (path[path_len] == '\0') break;
    }
    if (path_len == 0) return NULL;

    char * duped = kalloc(path_len+1); // +1 for the null byte we need to copy
    kassert(duped);

    memcpy(duped, path, path_len+1);
    *len_out = path_len;
    return duped;
}


// TODO: check permissions
// note: if file is open for searching only, check the directory permissions

int openat_inode(inode_t * base, const char * path, unsigned short flags, unsigned short mode, inode_t ** out);
int sys_open(const char * path, unsigned short flags, unsigned short mode) {
    inode_t * new = NULL;
    int ret = openat_inode(current_process->pwd, path, flags, mode, &new);
    if (ret < 0) return ret;
    ret = get_fd_from_inode(new, flags);
    if (ret < 0)
        close_inode(new);
    return ret;
}

int sys_openat(int fd, const char * path, unsigned short flags, unsigned short mode) {
    if ((fd < 0 || fd >= FD_LIMIT_PROCESS) && fd != AT_FDCWD) return -EBADF;

    inode_t * ino = NULL;
    if (fd != AT_FDCWD) {
        file_descriptor_t * file = current_process->fds[fd];
        if (file == NULL) return -EBADF;
        kassert(file->instances > 0);
        ino = file->inode;
    } else
        ino = current_process->pwd;

    kassert(ino != NULL);
    kassert(ino->instances > (ino->is_mountpoint)? 1 : 0);

    inode_t * new = NULL;
    int ret = openat_inode(ino, path, flags, mode, &new);
    if (ret < 0) return ret;
    ret = get_fd_from_inode(new, flags);
    if (ret < 0)
        close_inode(new);
    return ret;
}

int openat_inode(inode_t * base, const char * path, unsigned short flags, unsigned short mode, inode_t ** out) {
    if (base == NULL) {
        kprintf("\e[41mWarning: called openat with NULL base inode!\e[0m\n");
        return -EINVAL;
    }
    if (current_process->root == NULL) {
        kprintf("\e[41mWarning: called openat with NULL root inode!\e[0m\n");
        return -EINVAL;
    }
    if (current_process->pwd == NULL) {
        kprintf("\e[41mWarning: called openat with NULL pwd inode!\e[0m\n");
        return -EINVAL;
    }
    if (path == NULL) return -EINVAL;
    kassert(root_mountpoint);
    int ret = 0;

    size_t pathlen;
    char * dup_path = secure_strdup(path, PATH_MAX, &pathlen);
    if (dup_path == NULL) return -EFAULT;

    // deciding whether normal path (/,///+) or meta directory //
    if (
        dup_path[0] == '/' &&
        pathlen >= sizeof(PATH_METADIR) &&
        strcmp(dup_path, PATH_METADIR) == 0 &&
        dup_path[sizeof(PATH_METADIR) - 1] != '/'
    ) {
        kprintf("Stub: we don't yet support the // meta directory!\n");
        kfree(dup_path);
        return -EINVAL;
    }

    // path cleanup
    char seen_slash = 0;
    for (size_t i = 0; i < pathlen; i++) {
        if (dup_path[i] == '/') {
            if (seen_slash) {
                // move everything left by one to remove second slash
                memmove(dup_path + i, dup_path + i + 1, pathlen - i - 1);
                pathlen --;
                i--; // to counteract the i++
                dup_path[pathlen] = '\0';
            } else seen_slash = 1;
        } else seen_slash = 0;
    }
    // cleanup of trailing slashes
    for (size_t i = pathlen - 1; i >= 1; i--) { // >= 1 to not remove the first /
        if (dup_path[i] == '/') {
            dup_path[i] = '\0';
            pathlen--;
        } else break;
    }


    // resolving the mount point
    spinlock_acquire(&mount_tree_lock);

    char * final_path = dup_path;

    superblock_t * sb;
    if (dup_path[0] == '/') {
        final_path ++; // skip the first slash
        base = current_process->root;
    }

    spinlock_acquire(&current_process->lock);

    // raced with close() on the base inode
    if (base->instances <= (base->is_mountpoint) ? 1 : 0) {
        ret = -EINVAL;
        goto err;
    }


    inode_t * prev = base, * new = NULL;

    if (prev->is_mountpoint && prev != root_mountpoint->mountpoint) {
        sb = prev->next_superblock;
    }
    else {
        sb = prev->backing_superblock;
        // our while loop closes the prev inode, so preincrement
        __atomic_add_fetch(&prev->instances, 1, __ATOMIC_RELAXED);
    }

    kassert(sb->funcs);
    kassert(sb->funcs->lookup);

    // so that prev is never null, simplifying chroot checks
    if (prev->is_mountpoint && prev != root_mountpoint->mountpoint) {
        prev = sb->funcs->lookup(sb, NULL, ".");
    }

    kassert(prev->is_mountpoint == 0 || prev == root_mountpoint->mountpoint);

    char last_fragment = 0;
    while (!last_fragment) {
        char * next_slash = strchrnul(final_path, '/');
        if (*next_slash == '\0') last_fragment = 1;
        *next_slash = '\0';

        if (last_fragment) {
            if (sb->mount_options & MOUNT_RDONLY && mode & O_WRONLY) {
                close_inode(prev);
                ret = -EROFS;
                goto err;
            }
        }
        if (next_slash - final_path == 2 &&
            strcmp(PATH_PARENT, final_path) == 0 &&
            prev == current_process->root) {
                if (last_fragment) {
                    new = prev;
                    break;
                }
                final_path = next_slash + 1;
                continue;
            }
        new = sb->funcs->lookup(sb, prev, final_path);

        if (new == VFS_LOOKUP_NOTFOUND) {
            if (last_fragment &&
                sb->funcs->create != NULL &&
                flags & O_CREAT)
            {
                if ((sb->mount_options & O_RDONLY)) {
                    close_inode(prev);
                    ret = -EROFS;
                    goto err;
                }
                new = sb->funcs->create(sb, prev, final_path, mode);
                close_inode(prev);
                break;
            }
            close_inode(prev);
            ret = -ENOENT;
            goto err;
        }
        if (new == VFS_LOOKUP_ESCAPE) {
            new = sb->mountpoint;
            __atomic_add_fetch(&new->instances, 1, __ATOMIC_RELAXED);
            sb = new->backing_superblock;
            if (last_fragment) {
                close_inode(prev);
                break;
            }
        }
        if (new == VFS_LOOKUP_NOTDIRECTORY) {
            close_inode(prev);
            ret = -ENOTDIR;
            goto err;
        }
        if (new->is_mountpoint) {
            sb = new->next_superblock;
            kassert(sb);
            kassert(sb->funcs);
            close_inode(new);

            if (sb->funcs->lookup == NULL) {
                close_inode(prev);
                ret = -EINVAL;
                goto err;
            }

            new = sb->funcs->lookup(sb, NULL, ".");
        }
        if (last_fragment) {
            close_inode(prev);
            break;
        }
        close_inode(prev);
        prev = new;
        final_path = next_slash + 1;
    }

    if (flags & O_DIRECTORY && !S_ISDIR(new->mode)) {
        close_inode(new);
        ret = -ENOTDIR;
        goto err;
    }

    *out = new;

    err:
    spinlock_release(&current_process->lock);
    spinlock_release(&mount_tree_lock);
    kfree(dup_path);
    return ret;
}

int sys_chdir(const char * path) {
    inode_t * new = NULL;
    int ret = openat_inode(current_process->pwd, path, O_DIRECTORY | O_RDONLY, 0, &new);
    if (ret < 0) return ret;
    if (new == NULL) return -EINVAL;

    spinlock_acquire(&current_process->lock);
    inode_t * old_pwd = current_process->pwd;
    current_process->pwd = new;
    close_inode(old_pwd);
    spinlock_release(&current_process->lock);

    return 0;
}

int sys_chroot(const char * path) {
    inode_t * new = NULL;
    int ret = openat_inode(current_process->pwd, path, O_DIRECTORY | O_RDONLY, 0, &new);
    if (ret < 0) return ret;
    if (new == NULL) return -EINVAL;

    spinlock_acquire(&current_process->lock);
    inode_t * old_root = current_process->root;
    current_process->root = new;
    close_inode(old_root);
    spinlock_release(&current_process->lock);
    return 0;
}