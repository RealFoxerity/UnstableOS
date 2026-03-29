#include "../include/fs/fs.h"
#include "../include/fs/vfs.h"
#include "../include/kernel_sched.h"
#include "../include/errno.h"
#include "../../libc/src/include/string.h"
#include "../../libc/src/include/sys/stat.h"
#include "../include/kernel_tty_io.h"
#include "../include/block/memdisk.h"

#include <stddef.h>
spinlock_t kernel_fd_lock = {0};

file_descriptor_t ** kernel_fds;

void init_fds() {
    kernel_fds = kalloc(sizeof(file_descriptor_t *) * FD_LIMIT_KERNEL);
    kassert(kernel_fds);
    memset(kernel_fds, 0, sizeof(file_descriptor_t *) * FD_LIMIT_KERNEL);
}

// acquire lock before this, sets the fd instance count to 1
file_descriptor_t * get_free_fd() {
    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        if (kernel_fds[i] == NULL) {
            kernel_fds[i] = kalloc(sizeof(file_descriptor_t));
            if (kernel_fds[i] == NULL) panic("Not enough memory to allocate new file descriptor");
            memset(kernel_fds[i], 0, sizeof(file_descriptor_t));
            kernel_fds[i]->instances = 1;
            return kernel_fds[i];
        }
        else if (kernel_fds[i]->instances == 0) {
            memset(kernel_fds[i], 0, sizeof(file_descriptor_t));
            kernel_fds[i]->instances = 1;
            return kernel_fds[i];
        }
    }
    panic("No free file descriptors available!");
    //return NULL;
}

int get_fd_from_inode(inode_t * inode, unsigned short flags) {
    if (inode == NULL) return -EINVAL;
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

    file_descriptor_t * file = get_free_fd();
    if (!file) {
        spinlock_release(&kernel_fd_lock);
        return -ENFILE;
    }

    file->inode = inode;
    file->flags = flags;
    current_process->fds[fd] = file;
    spinlock_release(&kernel_fd_lock);
    return fd;
}

static int check_file(const file_descriptor_t * file) {
    if (file == NULL) return -EBADF;

    kassert(file->instances > 0);
    kassert(file->inode != NULL);
    kassert(file->inode->instances > (file->inode->is_mountpoint)? 1 : 0);

    return 0;
}

int sys_close(int fd) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];
    if (file == NULL) return -EBADF;

    current_process->fds[fd] = NULL;
    return close_file(file);
}

// when we know for 100% that the file isn't used anywhere in the running process
// (for example the scheduler when destroying and application)
// so that we don't have to needlessly wait
int close_file_forced(file_descriptor_t * file) {
    inode_t * inode = file->inode; // to not race
    if (__atomic_sub_fetch(&file->instances, 1, __ATOMIC_RELAXED) == 0) close_inode(inode);
}

int close_file(file_descriptor_t * file) {
    spinlock_acquire(&file->access_lock);
    spinlock_acquire(&kernel_fd_lock);
    close_file_forced(file);
    // call inode_cleanup() maybe
    spinlock_release(&file->access_lock); // has to be before kernel_fd_lock, otherwise UAF
    spinlock_release(&kernel_fd_lock);
    return 0;
}

ssize_t read_file(file_descriptor_t *file, void *buf, size_t count) {
    int test = check_file(file);
    if (test != 0) return test;
    if (file->flags & O_SEARCH) return -EBADF;

    if (S_ISDIR(file->inode->mode)) return -EISDIR;
    if (!(file->flags & O_RDONLY)) return -EINVAL;
    if (count == 0) return 0;

    if (count > SSIZE_MAX) { return -E2BIG; }

    ssize_t ret = 0;
    spinlock_acquire_interruptible(&file->access_lock);
    if (S_ISFIFO(file->inode->mode)) {
        ret = pipe_read(file, buf, count);
    } else if (!(S_ISBLK(file->inode->mode) || S_ISCHR(file->inode->mode))) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (file->inode->backing_superblock->funcs->read == NULL)
            ret = -EINVAL;
        else
            ret = file->inode->backing_superblock->funcs->read(file, buf, count);
    } else {
        switch (MAJOR(file->inode->device)) {
            case DEV_MAJ_TTY:
                ret = tty_read(file->inode->device, buf, count);
                break;
            case DEV_MAJ_MEM:
                ret = memdisk_read(file, buf, count);
                break;
            case DEV_MAJ_MISC:
                switch (MINOR(file->inode->device)) {
                    case DEV_MISC_PS2MOUSE:
                        ret = ps2_mouse_read(buf, count);
                        break;
                    case DEV_MISC_NULL:
                        ret = 0;
                        break;
                    case DEV_MISC_ZERO:
                        memset(buf, 0, count);
                        ret = count;
                        break;
                    default:
                        ret = -EIO;
                }
                break;
            default:
                kprintf("unknown dev major to read from (%d)...\n", MAJOR(file->inode->device));
                ret = -EIO;
        }
    }
    spinlock_release(&file->access_lock);
    return ret;
}

ssize_t write_file(file_descriptor_t *file, const void *buf, size_t count) {
    int test = check_file(file);
    if (test != 0) return test;
    if (file->flags & O_SEARCH) return -EBADF;

    if (S_ISDIR(file->inode->mode)) return -EISDIR;
    if (!(file->flags & O_WRONLY))
        return -EINVAL;

    if (count > SSIZE_MAX) { return -E2BIG; }
    if (count == 0) return 0;

    ssize_t ret = 0;
    spinlock_acquire_interruptible(&file->access_lock);

    if (S_ISFIFO(file->inode->mode)) {
        ret = pipe_write(file, buf, count);
    } else if (!(S_ISBLK(file->inode->mode) || S_ISCHR(file->inode->mode))) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (!(file->inode->backing_superblock->mount_options & MOUNT_RDONLY)) {
            kprintf("Warning: File descriptor marked writable on read-only fs, readjusting...\n");
            file->flags ^= O_WRONLY;
            spinlock_release(&file->access_lock);
            return -EINVAL;
        }
        if (file->inode->backing_superblock->funcs->write == NULL)
            ret = -EINVAL;
        else
            ret = file->inode->backing_superblock->funcs->write(file, buf, count);
    } else {
        switch (MAJOR(file->inode->device)) {
            case DEV_MAJ_TTY:
                ret = tty_write(file->inode->device, buf, count);
                break;
            case DEV_MAJ_MEM:
                ret = memdisk_write(file, buf, count);
                break;
            case DEV_MAJ_MISC:
                switch (MINOR(file->inode->device)) {
                    case DEV_MISC_NULL:
                    case DEV_MISC_ZERO:
                        ret = count;
                        break;
                    default:
                        ret = -EIO;
                }
                break;
            default:
                kprintf("unknown dev major to write to (%d)...\n", MAJOR(file->inode->device));
                ret = -EIO;
        }
    }

    spinlock_release(&file->access_lock);
    return ret;
}

off_t seek_file(file_descriptor_t * file, off_t offset, int whence) {
    int test = check_file(file);
    if (test != 0) return test;

    switch (whence) {
        case SEEK_SET:
        case SEEK_CUR:
        case SEEK_END:
            break;
        default:
            return -EINVAL;
    }

    ssize_t ret = 0;
    if (S_ISFIFO(file->inode->mode)) { return -ESPIPE; }
    spinlock_acquire_interruptible(&file->access_lock);
    if (!(S_ISBLK(file->inode->mode) || S_ISCHR(file->inode->mode))) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (file->inode->backing_superblock->funcs->seek == NULL)
            ret = -EINVAL;
        else
            ret = file->inode->backing_superblock->funcs->seek(file, offset, whence);
    } else if (S_ISBLK(file->inode->mode) || S_ISCHR(file->inode->mode)) {
        switch (MAJOR(file->inode->device)) {
            case DEV_MAJ_MEM:
                ret = memdisk_seek(file, offset, whence);
                break;

            default:
                kprintf("unknown dev major or not seekable (%d)...\n", MAJOR(file->inode->device));
            case DEV_MAJ_TTY:
                ret = -ESPIPE;
        }
    } else {
        ret = -ESPIPE; // assuming file is not seekable
    }
    spinlock_release(&file->access_lock);
    return ret;
}

ssize_t sys_read(int fd, void * buf, size_t count) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];

    return read_file(file, buf, count);
}

ssize_t sys_write(int fd, const void * buf, size_t count) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];
    return write_file(file, buf, count);
}

off_t sys_seek(int fd, off_t offset, int whence) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];

    return seek_file(file, offset, whence);
}


int sys_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= FD_LIMIT_PROCESS) return -EBADF;

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

    current_process->fds[fd] = current_process->fds[oldfd];
    __atomic_add_fetch(&current_process->fds[fd]->instances, 1, __ATOMIC_RELAXED);

    spinlock_release(&kernel_fd_lock);

    return fd;
}

int sys_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= FD_LIMIT_PROCESS) return -EBADF;
    if (newfd < 0 || newfd >= FD_LIMIT_PROCESS) return -EBADF;

    spinlock_acquire(&kernel_fd_lock);

    if (current_process->fds[newfd] != NULL) { // close the fd
        close_file(current_process->fds[newfd]);
        current_process->fds[newfd] = NULL;
    }

    current_process->fds[newfd] = current_process->fds[oldfd];
    __atomic_add_fetch(&current_process->fds[oldfd]->instances, 1, __ATOMIC_RELAXED);

    spinlock_release(&kernel_fd_lock);

    return newfd;
}



ssize_t sys_readdir(int fd, struct dirent * dent, size_t dent_size) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];
    int test = check_file(file);
    if (test != 0) return test;

    if (!S_ISDIR(file->inode->mode)) return -ENOTDIR;

    if (!file->inode->backing_superblock->funcs->readdir) return -EINVAL;

    spinlock_acquire_interruptible(&file->access_lock);
    ssize_t ret = file->inode->backing_superblock->funcs->readdir(file, dent, dent_size);
    spinlock_release(&file->access_lock);

    return ret;
}

int stat_inode(inode_t * inode, struct stat * buf) {
    kassert(buf)
    kassert(inode);
    kassert(inode->backing_superblock);
    kassert(inode->backing_superblock->funcs);
    if(inode->backing_superblock->funcs->stat == NULL)
        return -EINVAL;

    return inode->backing_superblock->funcs->stat(inode, buf);
}

int sys_fstat(int fd, struct stat * buf) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;
    file_descriptor_t * file = current_process->fds[fd];
    int test = check_file(file);
    if (test != 0) return test;

    return stat_inode(file->inode, buf);
}

int sys_fstatat(int fd, const char * __restrict path, struct stat * __restrict buf, int flags) {
    inode_t * base = NULL;
    inode_t * new = NULL;

    if (fd == AT_FDCWD)
        base = current_process->pwd;
    else {
        if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;
        int test = check_file(current_process->fds[fd]);
        if (test < 0) return test;
        base = current_process->fds[fd]->inode;
    }

    int ret = openat_inode(base, path, O_SEARCH, 0, &new);
    if (ret < 0) return ret;
    ret = stat_inode(new, buf);
    close_inode(new);
    return ret;
}