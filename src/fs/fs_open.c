#include "fs/fs.h"
#include "fs/vfs.h"
#include "kernel_sched.h"
#include <errno.h>
#include "mm/kernel_memory.h"

#include <string.h>
#include <limits.h>
#include "kernel_tty_io.h"
#include "block/memdisk.h"
#include <fcntl.h>


static int __open_raw_device(dev_t device, unsigned short flags, file_descriptor_t ** file_out) {
    kassert(file_out);
    file_descriptor_t * file = get_free_fd();
    if (!file) {
        return -ENFILE;
    }

    inode_t * dev_inode = NULL;
    long status = inode_from_device(device, &dev_inode);
    if (status < 0) {
        file->instances = 0; // free the fd
        return status;
    }

    file->inode = dev_inode;
    file->flags = flags;
    *file_out = file;
    return 0;
}
int open_raw_device(dev_t device, unsigned short flags, file_descriptor_t ** file_out) {
    int ret = 0;
    spinlock_acquire(&kernel_fd_lock);

    ret = __open_raw_device(device, flags, file_out);

    spinlock_release(&kernel_fd_lock);
    return ret;
}
int open_raw_device_fd(dev_t device, unsigned short flags) {
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

    PAGE_TABLE_TYPE * pte = paging_get_pte(addr);
    if (pte == NULL) return 0;
    if (*pte == 0) return 0; // not present
    if (current_process->pid != 0 && !(*pte & PTE_PDE_PAGE_USER_ACCESS)) return 0; // userspace doesn't have access

    return 1;
}

char * secure_strdup(const char * path, size_t max_len, size_t *len_out) {
    if (path == NULL) return NULL;

    VM_LOCK(path);
    size_t path_len;
    for (path_len = 0; path_len < max_len; path_len++) {
        if (!check_page(path + path_len)) goto error;
        if (path[path_len] == '\0') break;
    }
    if (path_len == 0) goto error;

    char * duped = kalloc(path_len+1); // +1 for the null byte we need to copy
    kassert(duped);

    memcpy(duped, path, path_len+1);
    duped[path_len] = '\0';
    *len_out = path_len;
    VM_UNLOCK(path);
    return duped;

    error:
    VM_UNLOCK(path);
    return NULL;
}


// TODO: check permissions
// note: if file is open for searching only, check the directory permissions

int sys_openat(int fd, const char * path, unsigned short flags, mode_t mode) {
    if ((fd < 0 || fd >= FD_LIMIT_PROCESS) && fd != AT_FDCWD) return -EBADF;

    inode_t * ino = NULL;
    if (fd != AT_FDCWD) {
        file_descriptor_t * file = current_process->fds[fd];
        if (file == NULL) return -EBADF;
        kassert(file->instances > 0);
        ino = file->inode;
    } else
        ino = (inode_t*)AT_FDCWD;

    kassert(ino != NULL);
    kassert(ino->instances > (ino->is_mountpoint ? 1 : 0));

    inode_t * new = NULL;
    int ret = openat_inode(ino, path, flags, mode, &new, 0);
    if (ret < 0) return ret;
    ret = get_fd_from_inode(new, flags);
    if (ret < 0)
        close_inode(new);
    return ret;
}

size_t cleanup_path(char * dup_path, size_t pathlen) {
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
        if (dup_path[i] == '/' && dup_path[i-1] == '/') {
            dup_path[i] = '\0';
            pathlen--;
        } else break;
    }

    return pathlen;
}

int openat_inode(inode_t * base, const char * path, unsigned int flags, mode_t mode, inode_t ** out, char trusted_path) {
    if (base == NULL) {
        kprintf("\e[0m\e[41mWarning: called openat with NULL base inode!\e[0m\n");
        return -EINVAL;
    }
    if (current_process->root == NULL) {
        kprintf("\e[0m\e[41mWarning: called openat with NULL root inode!\e[0m\n");
        return -EINVAL;
    }
    char preincremented = 0;
    if (base == (inode_t*)AT_FDCWD) {
        spinlock_acquire(&current_process->lock);
        base = current_process->pwd;
        kassert(base);
        __atomic_add_fetch(&base->instances, 1, __ATOMIC_ACQUIRE);
        spinlock_release(&current_process->lock);
        preincremented = 1;
    }

    if (!S_ISDIR(base->mode)) {
        if (preincremented)
            close_inode(base);
        return -ENOTDIR;
    }

    if (path == NULL) {
        if (preincremented)
            close_inode(base);
        return -EFAULT;
    }
    if (flags & (O_PATH | O_SEARCH)) {
        flags &= O_PATH | O_DIRECTORY | O_SEARCH | O_CLOEXEC | O_CLOFORK;
        if (flags & O_SEARCH)
            flags |= O_DIRECTORY;
    }
    mode &= ~current_process->umask;
    mode &= ~S_IFMT;

    kassert(root_mountpoint);
    int ret = 0;

    size_t pathlen;
    char * dup_path;
    if (trusted_path) { // to avoid pid >0 in paths in the kernel from failing secure_strdup
        pathlen = strlen(path);
        dup_path = strdup(path);
    } else
        dup_path = secure_strdup(path, PATH_MAX, &pathlen);
    if (dup_path == NULL || pathlen == 0) {
        kfree(dup_path);
        if (preincremented)
            close_inode(base);
        return -EFAULT;
    }

    // deciding whether normal path (/,///+) or meta directory //
    if (strcmp(dup_path, PATH_METADIR) == 0) {
        kprintf("Stub: we don't yet support the // meta directory!\n");
        kfree(dup_path);
        if (preincremented)
            close_inode(base);
        return -EINVAL;
    }


    pathlen = cleanup_path(dup_path, pathlen);

    char need_dir = dup_path[pathlen - 1] == '/';

    // resolving the mount point
    spinlock_acquire(&mount_tree_lock);

    char * final_path = dup_path;

    spinlock_acquire(&current_process->lock);
    inode_t * current_root = current_process->root;
        kassert(current_root);
    __atomic_add_fetch(&current_root->instances, 1, __ATOMIC_ACQUIRE);
    spinlock_release(&current_process->lock);

    superblock_t * sb;
    if (dup_path[0] == '/') {
        final_path ++; // skip the first slash
        if (preincremented) {
            close_inode(base);
            preincremented = 0;
        }
        base = current_root;
    }

    // raced with close() on the base inode
    if (base->instances <= (base->is_mountpoint ? 1 : 0)) {
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
        if (!preincremented)
            __atomic_add_fetch(&prev->instances, 1, __ATOMIC_ACQUIRE);
    }

    kassert(sb->funcs);
    kassert(sb->funcs->lookup);

    // so that prev is never null, simplifying chroot checks
    if (prev->is_mountpoint && prev != root_mountpoint->mountpoint) {
        inode_t * prev_back = prev;
        sb->funcs->lookup(sb, NULL, ".", &prev, O_SEARCH | O_DIRECTORY);
        if (preincremented)
            close_inode(prev_back);
    }

    kassert(prev->is_mountpoint == 0 || prev == root_mountpoint->mountpoint);

    char last_fragment = 0;
    while (!last_fragment) {
        char * next_slash = strchrnul(final_path, '/');
        if (*next_slash == '\0' || *(next_slash + 1) == '\0') last_fragment = 1;
        *next_slash = '\0';

        lookup_escape_again:
        if (strcmp(PATH_PARENT, final_path) == 0 &&
            prev == current_root) {
                if (last_fragment) {
                    new = prev;
                    break;
                }
                final_path = next_slash + 1;
                continue;
            }
        long status = sb->funcs->lookup(sb, prev, final_path, &new, last_fragment ? flags : (O_SEARCH | O_DIRECTORY));

        if (status == -ENOENT) {
            if (last_fragment &&
                ((!(flags & O_DIRECTORY) && sb->funcs->creat != NULL) ||
                    (flags & O_DIRECTORY && sb->funcs->mkdir != NULL)) &&
                flags & O_CREAT)
            {
                if (sb->mount_options & MOUNT_RDONLY) {
                    close_inode(prev);
                    ret = -EROFS;
                    goto err;
                }

                if (flags & O_DIRECTORY)
                    status = sb->funcs->mkdir(prev, final_path, mode, &new);
                else
                    status = sb->funcs->creat(prev, final_path, mode, &new);

                if (status == 0) {
                    utimes_inode(prev,
                        (struct timespec){.tv_nsec = UTIME_OMIT},
                        (struct timespec){.tv_nsec = UTIME_NOW},
                        (struct timespec){.tv_nsec = UTIME_NOW});
                }

                close_inode(prev);
                if (status < 0) {
                    ret = status;
                    goto err;
                }
                if (new == NULL) {
                    ret = -ENXIO;
                    goto err;
                }
                break;
            }
            close_inode(prev);
            ret = -ENOENT;
            goto err;
        }
        if (status < 0) {
            close_inode(prev);
            ret = status;
            goto err;
        }
        if (new && last_fragment && need_dir && !S_ISDIR(new->mode)) {
            close_inode(prev);
            close_inode(new);
            ret = -ENOTDIR;
            goto err;
        }
        // has to be below because we prefer returning ENOENT
        // devices are not governed by the mountpoint options
        if (new && last_fragment && (S_ISREG(new->mode) || S_ISDIR(new->mode))) {
            if ((sb->mount_options & MOUNT_RDONLY ||
                sb->funcs->pwrite == NULL)
                    && flags & O_WRONLY) {
                close_inode(prev);
                ret = -EROFS;
                goto err;
            }
        }
        if (status == VFS_LOOKUP_ESCAPE) {
            new = sb->mountpoint;
            __atomic_add_fetch(&new->instances, 1, __ATOMIC_ACQUIRE);
            sb = new->backing_superblock;
            close_inode(prev);

            prev = new;
            goto lookup_escape_again; // try again on parent superblock
        }
        if (new == NULL) {
            close_inode(prev);
            ret = -ENXIO;
            goto err;
        }
        if (last_fragment &&
            flags & O_CREAT && (
                flags & O_EXCL || flags & O_DIRECTORY
            )
        ) {
            close_inode(prev);
            close_inode(new);
            ret = -EEXIST;
            goto err;
        }
        if (last_fragment && flags & O_NOXDEV) {
            close_inode(prev);
            break;
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

            status = sb->funcs->lookup(sb, NULL, ".", &new, O_SEARCH | O_DIRECTORY);
            if (status < 0) {
                close_inode(prev);
                ret = status;
                goto err;
            }
            if (new == NULL) {
                close_inode(prev);
                ret = -ENXIO;
                goto err;
            }
        }
        if (last_fragment) {
            close_inode(prev);
            if (S_ISREG(new->mode) && flags & O_TRUNC && flags & O_WRONLY) {
                if (new->backing_superblock->funcs->trunc) {
                    status = new->backing_superblock->funcs->trunc(new, 0);
                    if (status != 0) {
                        ret = status;
                        close_inode(prev);
                        close_inode(new);
                        goto err;
                    }
                    if (status == 0) {
                        utimes_inode(new,
                            (struct timespec){.tv_nsec = UTIME_OMIT},
                            (struct timespec){.tv_nsec = UTIME_NOW},
                            (struct timespec){.tv_nsec = UTIME_NOW});
                    }
                }
            }
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
    close_inode(current_root);
    spinlock_release(&mount_tree_lock);
    kfree(dup_path);
    return ret;
}

int openat_file(inode_t * base, const char * path, unsigned int flags, mode_t mode, file_descriptor_t ** out, char trusted_path) {
    kassert(out);
    file_descriptor_t * file = get_free_fd();
    if (!file)
        return -ENFILE;

    inode_t * inode = NULL;
    int file_status = openat_inode(base, path, flags, mode, &inode, trusted_path);
    if (file_status < 0 || inode == NULL) {
        file->instances = 0;
        return file_status;
    }
    file->inode = inode;
    file->flags = flags;
    *out = file;
    return 0;
}

int sys_chdir(const char * path) {
    inode_t * new = NULL;
    spinlock_acquire(&current_process->lock);
    inode_t * curr_pwd = current_process->pwd; // not race with another chdir
    __atomic_add_fetch(&curr_pwd->instances, 1, __ATOMIC_ACQUIRE);
    spinlock_release(&current_process->lock);

    int ret = openat_inode(curr_pwd, path, O_DIRECTORY | O_RDONLY, 0, &new, 0);
    close_inode(curr_pwd);

    if (ret < 0) return ret;
    if (new == NULL) return -EINVAL;

    spinlock_acquire(&current_process->lock);
    inode_t * old_pwd = current_process->pwd;
    current_process->pwd = new;
    spinlock_release(&current_process->lock);
    close_inode(old_pwd);

    return 0;
}

int sys_chroot(const char * path) {
    inode_t * new = NULL;
    spinlock_acquire(&current_process->lock);
    inode_t * curr_pwd = current_process->pwd; // not race with chdir
    __atomic_add_fetch(&curr_pwd->instances, 1, __ATOMIC_ACQUIRE);
    spinlock_release(&current_process->lock);

    int ret = openat_inode(curr_pwd, path, O_DIRECTORY | O_RDONLY, 0, &new, 0);
    close_inode(curr_pwd);
    if (ret < 0) return ret;
    if (new == NULL) return -EINVAL;

    spinlock_acquire(&current_process->lock);
    inode_t * old_root = current_process->root;
    current_process->root = new;
    spinlock_release(&current_process->lock);
    close_inode(old_root);
    return 0;
}

static char is_valid_rename(const char * pathname) {
    const char * last_slash = strrchr(pathname, '/');
    if (last_slash == NULL) {
        if (strcmp(".", pathname) == 0 || strcmp("..", pathname) == 0) {
            return -EINVAL;
        }
    } else {
        if (*(last_slash + 1) == '\0') { // /a/
            const char * next = strrchr(last_slash - 1, '/');
            if (next == NULL)
                next = pathname;

            if (strcmp("./", next) == 0 || strcmp("../", next) == 0) {
                return -EINVAL;
            }
        } else {
            if (strcmp(".", last_slash + 1) == 0 || strcmp("..", last_slash + 1) == 0) {
                return -EINVAL;
            }
        }
    }
    return 0;
}
int sys_renameat(int oldfd, const char * old, int newfd, const char * new) {
    inode_t * old_parent = NULL;
    inode_t * new_parent = NULL;

    if (old == NULL || new == NULL)
        return -EFAULT;

    if ((oldfd < 0 || oldfd >= FD_LIMIT_PROCESS) && oldfd != AT_FDCWD) return -EBADF;
    if ((newfd < 0 || newfd >= FD_LIMIT_PROCESS) && newfd != AT_FDCWD) return -EBADF;

    size_t old_len = 0;
    char * old_dup = secure_strdup(old, PATH_MAX, &old_len);
    if (old_dup == NULL || old_len == 0) {
        kfree(old_dup);
        return -EFAULT;
    }
    old_len = cleanup_path(old_dup, old_len);

    if (memcmp("//", old_dup, 2) == 0) {
        kprintf("Stub: we don't yet support the // meta directory!\n");
        kfree(old_dup);
        return -EINVAL;
    }

    if (strcmp("/", old_dup) == 0) {
        kfree(old_dup);
        return -EBUSY;
    }

    if (is_valid_rename(old_dup)) {
        kfree(old_dup);
        return -EINVAL;
    }

    size_t new_len = 0;
    char * new_dup = secure_strdup(new, PATH_MAX, &new_len);
    if (new_dup == NULL || new_len == 0) {
        kfree(old_dup);
        kfree(new_dup);
        return -EBUSY;
    }
    new_len = cleanup_path(new_dup, new_len);

    if (memcmp("//", new_dup, 2) == 0) {
        kprintf("Stub: we don't yet support the // meta directory!\n");
        kfree(old_dup);
        kfree(new_dup);
        return -EINVAL;
    }

    if (strcmp("/", new_dup) == 0) {
        kfree(old_dup);
        kfree(new_dup);
        return -EBUSY;
    }

    if (is_valid_rename(new_dup)) {
        kfree(old_dup);
        kfree(new_dup);
        return -EINVAL;
    }

    spinlock_acquire(&current_process->lock);
    if (oldfd != AT_FDCWD) {
        file_descriptor_t * file = current_process->fds[oldfd];
        if (file == NULL) {
            spinlock_release(&current_process->lock);
            kfree(old_dup);
            kfree(new_dup);
            return -EBADF;
        }
        kassert(file->instances > 0);
        old_parent = file->inode;
    } else {
        old_parent = current_process->pwd;
    }

    if (newfd != AT_FDCWD) {
        file_descriptor_t * file = current_process->fds[newfd];
        if (file == NULL) {
            spinlock_release(&current_process->lock);
            kfree(old_dup);
            kfree(new_dup);
            return -EBADF;
        }
        kassert(file->instances > 0);
        new_parent = file->inode;
    } else {
        new_parent = current_process->pwd;
    }
    __atomic_add_fetch(&old_parent->instances, 1, __ATOMIC_ACQUIRE);
    __atomic_add_fetch(&new_parent->instances, 1, __ATOMIC_ACQUIRE);
    spinlock_release(&current_process->lock);

    int ret = 0;

    char should_be_dir = 0;
    char * old_frag = strrchr(old_dup, '/');
    if (old_frag && *(old_frag+1) == '\0') {
        should_be_dir = 1;
        *old_frag = '\0';
        old_frag = strrchr(old_dup, '/');
    }
    if (old_frag == NULL) {
        old_frag = old_dup;
    }
    if (*old_frag == '/') {
        *old_frag = '\0';
        old_frag++;
    }

    char * new_frag = strrchr(new_dup, '/');
    if (new_frag && *(new_frag+1) == '\0') {
        should_be_dir = 1;
        *new_frag = '\0';
        new_frag = strrchr(new_dup, '/');
    }
    if (new_frag == NULL) {
        new_frag = new_dup;
    }
    if (*new_frag == '/') {
        *new_frag = '\0';
        new_frag++;
    }


    if (*old_dup == '\0') {
        close_inode(old_parent);
        spinlock_acquire(&current_process->lock);
        old_parent = current_process->pwd;
        __atomic_add_fetch(&old_parent->instances, 1, __ATOMIC_ACQUIRE);
        spinlock_release(&current_process->lock);
        kassert(old_parent);
    } else if (old_dup != old_frag) {
        inode_t * new_old_parent = NULL;
        ret = openat_inode(old_parent, old_dup, O_WRONLY | O_DIRECTORY, 0, &new_old_parent, 1);
        if (ret < 0 || new_old_parent == NULL)
            goto err;
        close_inode(old_parent);
        old_parent = new_old_parent;
    }

    if (*new_dup == '\0') {
        close_inode(new_parent);
        spinlock_acquire(&current_process->lock);
        new_parent = current_process->pwd;
        __atomic_add_fetch(&new_parent->instances, 1, __ATOMIC_ACQUIRE);
        spinlock_release(&current_process->lock);
        kassert(new_parent);
    } else if (new_dup != new_frag) {
        inode_t * new_new_parent = NULL;
        ret = openat_inode(new_parent, new_dup, O_WRONLY | O_DIRECTORY, 0, &new_new_parent, 1);
        if (ret < 0 || new_new_parent == NULL)
            goto err;
        close_inode(new_parent);
        new_parent = new_new_parent;
    }

    if (old_parent->backing_superblock != new_parent->backing_superblock) {
        ret = -EXDEV;
        goto err;
    }

    if (old_parent->backing_superblock->mount_options & MOUNT_RDONLY) {
        ret = -EROFS;
        goto err;
    }
    if (old_parent->backing_superblock->funcs->rename == NULL) {
        ret = -ENOTSUP;
        goto err;
    }

    // needed for the directory check and then later for the ancestor check and the mountpoint check
    inode_t * src;
    ret = openat_inode(old_parent, old_frag, O_PATH | O_NOXDEV | (should_be_dir ? O_DIRECTORY : 0), 0, &src, 1);
    if (ret != 0 || src == NULL)
        goto err;
    inode_t * dst = NULL;
    ret = openat_inode(new_parent, new_frag, O_PATH | O_NOXDEV, 0, &dst, 1);
    if (ret != 0 || src == NULL) {
        // ok
    } else {
        if (S_ISDIR(dst->mode) != S_ISDIR(src->mode)) {
            ret = should_be_dir ? -ENOTDIR : -EISDIR;
            close_inode(dst);
            goto err;
        }
    }

    if (src == dst) {
        close_inode(src);
        close_inode(dst);
        ret = 0;
        goto err;
    }

    // quick check right off the bat
    if (new_parent == src) {
        close_inode(src);
        close_inode(dst);
        ret = -EINVAL;
        goto err;
    }

    // now for the harder part
    // have to manually iterate to check if new is an ancestor of old
    inode_t * curr;
    inode_t * prev = dst ? dst : new_parent;

    __atomic_add_fetch(&prev->instances, 1, __ATOMIC_ACQUIRE);

    superblock_t * sb = prev->backing_superblock;
    kassert(sb->funcs);
    kassert(sb->funcs->lookup);


    while (1) {
        ret = sb->funcs->lookup(sb, prev, "..", &curr, 0);
        if (ret == VFS_LOOKUP_ESCAPE)
            break;
        if (ret < 0) {
            close_inode(prev);
            close_inode(src);
            close_inode(dst);
            goto err;
        }
        if (src == curr) {
            close_inode(curr);
            close_inode(prev);
            close_inode(src);
            close_inode(dst);
            ret = -EINVAL;
            goto err;
        }
        close_inode(prev);
        prev = curr;
    }
    close_inode(prev);

    spinlock_acquire(&mount_tree_lock);
    char mnt = dst ? dst->is_mountpoint : 0;
    close_inode(src);
    close_inode(dst);
    if (mnt) {
        spinlock_release(&mount_tree_lock);
        ret = -EBUSY;
        goto err;
    }
    ret = src->backing_superblock->funcs->rename(old_parent, old_frag, new_parent, new_frag);
    spinlock_release(&mount_tree_lock);


    if (ret == 0) {
        utimes_inode(new_parent,
            (struct timespec){.tv_nsec = UTIME_OMIT},
            (struct timespec){.tv_nsec = UTIME_NOW},
            (struct timespec){.tv_nsec = UTIME_NOW});
        utimes_inode(old_parent,
            (struct timespec){.tv_nsec = UTIME_OMIT},
            (struct timespec){.tv_nsec = UTIME_NOW},
            (struct timespec){.tv_nsec = UTIME_NOW});
    }

    err:
    close_inode(old_parent);
    close_inode(new_parent);
    kfree(old_dup);
    kfree(new_dup);
    return ret;
}

int sys_mknodat(int fd, const char *path, mode_t mode, dev_t dev) {
    if ((fd < 0 || fd >= FD_LIMIT_PROCESS) && fd != AT_FDCWD) return -EBADF;

    if (!S_ISBLK(mode) && !S_ISCHR(mode))
        return -EINVAL;
    dev &= 0x7FFF;
    if (S_ISCHR(mode))
        dev |= 0x8000;

    inode_t * base = NULL;

    if (path == NULL)
        return -EBADF;

    size_t path_len = 0;
    char * safe_path = secure_strdup(path, PATH_MAX, &path_len);
    if (safe_path == NULL) {
        kfree(safe_path);
        return -EFAULT;
    }
    if (path_len == 0) {
        kfree(safe_path);
        return -ENOENT; // WHYY POSIX, WHYYY
    }

    char * last_slash = strrchr(safe_path, '/');
    if (last_slash == NULL)
        last_slash = safe_path;
    else last_slash++;
    if (*last_slash == '\0') { // trailing slash
        kfree(safe_path);
        return -EINVAL;
    }
    if (strcmp(last_slash, ".") == 0 ||
        strcmp(last_slash, "..") == 0) {
        kfree(safe_path);
        return -EINVAL; // maybe EILSEQ? not sure
    }

    spinlock_acquire(&current_process->lock);
    if (fd != AT_FDCWD) {
        file_descriptor_t * file = current_process->fds[fd];
        if (file == NULL) {
            spinlock_release(&current_process->lock);
            kfree(safe_path);
            return -EBADF;
        }
        kassert(file->instances > 0);
        base = file->inode;
    } else {
        base = current_process->pwd;
    }
    __atomic_add_fetch(&base->instances, 1, __ATOMIC_ACQUIRE);
    spinlock_release(&current_process->lock);

    int ret = 0;
    last_slash = strrchr(safe_path, '/');
    if (last_slash == NULL) {
        last_slash = safe_path;

        do_mknod:
        if (!base->backing_superblock ||
            !base->backing_superblock->funcs ||
            !base->backing_superblock->funcs->mknod
        ) {
            ret = -ENOTSUP; // linux does EPERM
            goto end;
        }
        if (base->backing_superblock->mount_options & MOUNT_RDONLY) {
            ret = -EROFS;
            goto end;
        }
        ret = inode_check_perm(base, W_OK, AT_EACCESS);
        if (ret < 0)
            goto end;
        mode &= ~current_process->umask;
        ret = base->backing_superblock->funcs->mknod(
            base, last_slash, mode, dev);
        if (ret == 0)
            utimes_inode(base,
                (struct timespec){.tv_nsec = UTIME_OMIT},
                (struct timespec){.tv_nsec = UTIME_NOW},
                (struct timespec){.tv_nsec = UTIME_NOW});
        goto end;
    }

    *last_slash = 0;
    last_slash++;

    inode_t * final = NULL;
    ret = openat_inode(base, safe_path, O_DIRECTORY | O_RDWR, 0, &final, 1);
    if (ret < 0)
        goto end;

    close_inode(base);
    base = final;
    goto do_mknod;

    end:
    kfree(safe_path);
    close_inode(base);
    return ret;
}

int sys_linkat(int fd1, const char * path1, int fd2, const char * path2, int flag) {
    if ((fd1 < 0 || fd1 >= FD_LIMIT_PROCESS) && fd1 != AT_FDCWD) return -EBADF;
    if ((fd2 < 0 || fd2 >= FD_LIMIT_PROCESS) && fd2 != AT_FDCWD) return -EBADF;

    inode_t * file = NULL;
    inode_t * file_base = NULL;
    inode_t * base = NULL;

    if (path2 == NULL)
        return -EBADF;

    size_t path_len = 0;
    char * safe_path = secure_strdup(path2, PATH_MAX, &path_len);
    if (safe_path == NULL) {
        kfree(safe_path);
        return -EFAULT;
    }
    if (path_len == 0) {
        kfree(safe_path);
        return -ENOENT; // WHYY POSIX, WHYYY
    }

    char * last_slash = strrchr(safe_path, '/');
    if (last_slash == NULL)
        last_slash = safe_path;
    else last_slash++;
    if (*last_slash == '\0') { // trailing slash
        kfree(safe_path);
        return -EINVAL;
    }
    if (strcmp(last_slash, ".") == 0 ||
        strcmp(last_slash, "..") == 0) {
        kfree(safe_path);
        return -EINVAL; // maybe EILSEQ? not sure
    }

    spinlock_acquire(&current_process->lock);
    if (fd1 != AT_FDCWD) {
        file_descriptor_t * file2 = current_process->fds[fd1];
        if (file2 == NULL) {
            spinlock_release(&current_process->lock);
            kfree(safe_path);
            return -EBADF;
        }
        kassert(file2->instances > 0);
        file_base = file2->inode;
    } else {
        file_base = current_process->pwd;
    }
    if (fd2 != AT_FDCWD) {
        file_descriptor_t * file2 = current_process->fds[fd2];
        if (file2 == NULL) {
            spinlock_release(&current_process->lock);
            kfree(safe_path);
            return -EBADF;
        }
        kassert(file2->instances > 0);
        base = file2->inode;
    } else {
        base = current_process->pwd;
    }
    __atomic_add_fetch(&file_base->instances, 1, __ATOMIC_ACQUIRE);
    __atomic_add_fetch(&base->instances, 1, __ATOMIC_ACQUIRE);
    spinlock_release(&current_process->lock);

    int ret = 0;

    if (path1) {
        ret = openat_inode(file_base, path1, O_PATH, 0, &file, 0);
        if (ret < 0) {
            kfree(safe_path);
            close_inode(file_base);
            close_inode(base);
            return ret;
        }
        close_inode(file_base);
    } else {
        file = file_base;
    }

    last_slash = strrchr(safe_path, '/');
    if (last_slash == NULL) {
        last_slash = safe_path;

        do_link:
        if (!base->backing_superblock ||
            !base->backing_superblock->funcs ||
            !base->backing_superblock->funcs->link
        ) {
            ret = -ENOTSUP; // linux does EPERM
            goto end;
        }
        if (base->backing_superblock->mount_options & MOUNT_RDONLY) {
            ret = -EROFS;
            goto end;
        }
        ret = inode_check_perm(base, W_OK, AT_EACCESS);
        if (ret < 0)
            goto end;
        ret = base->backing_superblock->funcs->link(
            file, base, safe_path);
        if (ret == 0) {
            utimes_inode(file,
                (struct timespec){.tv_nsec = UTIME_OMIT},
                (struct timespec){.tv_nsec = UTIME_OMIT},
                (struct timespec){.tv_nsec = UTIME_NOW});
            utimes_inode(base,
                (struct timespec){.tv_nsec = UTIME_OMIT},
                (struct timespec){.tv_nsec = UTIME_NOW},
                (struct timespec){.tv_nsec = UTIME_NOW});
        }
        goto end;
    }

    *last_slash = 0;
    last_slash++;

    inode_t * final = NULL;
    ret = openat_inode(base, safe_path, O_PATH, 0, &final, 1);
    if (ret < 0)
        goto end;

    close_inode(base);
    base = final;
    goto do_link;

    end:
    kfree(safe_path);
    close_inode(file);
    close_inode(base);
    return ret;
}