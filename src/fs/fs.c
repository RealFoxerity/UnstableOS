#include "../include/fs/fs.h"
#include "../include/fs/vfs.h"
#include "../include/kernel_sched.h"
#include "../include/errno.h"
#include "../../libc/src/include/string.h"
#include "../include/kernel_tty_io.h"
#include "../include/block/memdisk.h"

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

static int check_file(file_descriptor_t * file) {
    if (file == NULL) return EBADF;

    kassert(file->instances > 0);
    kassert(file->inode != NULL);
    kassert(file->inode->instances > (file->inode->is_mountpoint)? 1 : 0);

    return 0;
}

int sys_close(int fd) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return EBADF;

    spinlock_acquire(&kernel_fd_lock);

    file_descriptor_t * file = current_process->fds[fd];
    
    __atomic_sub_fetch(&file->instances, 1, __ATOMIC_RELAXED);
    if (file->instances == 0) __atomic_sub_fetch(&file->inode->instances, 1, __ATOMIC_RELAXED);

    current_process->fds[fd] = NULL;
    // call inode_cleanup() maybe
    spinlock_release(&kernel_fd_lock);
    return 0;
}

ssize_t read_file(file_descriptor_t * file, void * buf, size_t count) {
    int test = check_file(file);
    if (test != 0) return test;
    
    if (I_ISDIR(file->inode->mode)) return EISDIR;
    if (!(file->mode & O_RDONLY)) return EINVAL;

    ssize_t ret = 0;
    spinlock_acquire(&file->access_lock);

    if (!file->inode->is_raw_device) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (file->inode->backing_superblock->funcs->read == NULL) 
            ret = EINVAL;
        else
            ret = file->inode->backing_superblock->funcs->read(file, buf, count);
    } else {
        switch (MAJOR(file->inode->device)) {
            case DEV_MAJ_TTY:
                ret = tty_read(file->inode->device, buf, count);
                break;
            case DEV_MAJ_MEM:
                ret =  memdisk_read(file, buf, count);
                break;
            default:
                kprintf("unknown dev major to read from (%d)...\n", MAJOR(file->inode->device));
                ret = EIO;
        }
    }
    spinlock_release(&file->access_lock);
    return ret;
}

ssize_t write_file(file_descriptor_t * file, const void * buf, size_t count) {
    int test = check_file(file);
    if (test != 0) return test;

    if (I_ISDIR(file->inode->mode)) return EISDIR;
    if (!(file->mode & O_WRONLY) || 
        file->inode->backing_superblock->mount_options & MOUNT_RDONLY)
            return EINVAL;

    ssize_t ret = 0;
    spinlock_acquire(&file->access_lock);

    if (!file->inode->is_raw_device) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (file->inode->backing_superblock->funcs->write == NULL) 
            ret = EINVAL;
        else
            ret = file->inode->backing_superblock->funcs->write(file, buf, count);
    } else {
        switch (MAJOR(file->inode->device)) {
            case DEV_MAJ_TTY:
                ret = tty_write(file->inode->device, buf, count);
                break;
            case DEV_MAJ_MEM:
                ret =  memdisk_write(file, buf, count);
                break;
            default:
                kprintf("unknown dev major to write to (%d)...\n", MAJOR(file->inode->device));
                ret = EIO;
        }
    }

    spinlock_release(&file->access_lock);
    return ret;
}

off_t seek_file(file_descriptor_t * file, off_t offset, int whence) {
    int test = check_file(file);
    if (test != 0) return test;

    if (I_ISDIR(file->inode->mode)) return EISDIR;

    switch (whence) {
        case SEEK_SET:
        case SEEK_CUR:
        case SEEK_END:
            break;
        default:
            return EINVAL;
    }

    ssize_t ret = 0;
    spinlock_acquire(&file->access_lock);

    if (!file->inode->is_raw_device) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (file->inode->backing_superblock->funcs->seek == NULL) 
            ret = EINVAL;
        else
            ret = file->inode->backing_superblock->funcs->seek(file, offset, whence);
    } else {
        switch (MAJOR(file->inode->device)) {
            case DEV_MAJ_MEM:
                ret = memdisk_seek(file, offset, whence);
                break;

            default:
                kprintf("unknown dev major or not seekable (%d)...\n", MAJOR(file->inode->device));
            case DEV_MAJ_TTY:
                ret = ESPIPE; // assuming file is not seekable
        }
    }
    spinlock_release(&file->access_lock);
    return ret;
}

ssize_t sys_read(int fd, void * buf, size_t count) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return EBADF;

    file_descriptor_t * file = current_process->fds[fd];

    return read_file(file, buf, count);
}

ssize_t sys_write(int fd, const void * buf, size_t count) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return EBADF;

    file_descriptor_t * file = current_process->fds[fd];
    return write_file(file, buf, count);
}

off_t sys_seek(int fd, off_t offset, int whence) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return EBADF;

    file_descriptor_t * file = current_process->fds[fd];
    
    return seek_file(file, offset, whence);
}


int sys_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= FD_LIMIT_PROCESS) return EBADF;

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
        return EMFILE;
    }

    current_process->fds[fd] = current_process->fds[oldfd];
    __atomic_add_fetch(&current_process->fds[fd]->instances, 1, __ATOMIC_RELAXED);

    spinlock_release(&kernel_fd_lock);

    return fd;
}

int sys_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= FD_LIMIT_PROCESS) return EBADF;
    if (newfd < 0 || newfd >= FD_LIMIT_PROCESS) return EBADF;

    spinlock_acquire(&kernel_fd_lock);

    if (current_process->fds[newfd] == 0) { // close the fd
        kassert(check_file(current_process->fds[newfd]) == 0);
        __atomic_sub_fetch(&current_process->fds[newfd]->instances, 1, __ATOMIC_RELAXED);
        if (current_process->fds[newfd]->instances == 0) 
            __atomic_sub_fetch(&current_process->fds[newfd]->inode->instances, 1, __ATOMIC_RELAXED);

        current_process->fds[newfd] = NULL;
    }

    current_process->fds[newfd] = current_process->fds[oldfd];
    __atomic_add_fetch(&current_process->fds[oldfd]->instances, 1, __ATOMIC_RELAXED);

    spinlock_release(&kernel_fd_lock);

    return newfd;
}