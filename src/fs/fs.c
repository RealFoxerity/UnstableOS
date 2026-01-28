#include "../include/fs/fs.h"
#include "../include/kernel_sched.h"
#include "../include/errno.h"
#include "../../libc/src/include/string.h"
#include "../include/kernel_tty_io.h"
spinlock_t kernel_fd_lock = {0};

file_descriptor_t * kernel_fds[FD_LIMIT_KERNEL] = {0};

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

static ssize_t check_fd(unsigned int fd) {
    if (fd >= FD_LIMIT_PROCESS) return EBADF;
    file_descriptor_t * file = current_process->fds[fd];
    if (file == NULL) return EBADF;

    kassert(file->instances > 0);
    kassert(file->inode != NULL);
    kassert(file->inode->instances > (file->inode->is_mountpoint)? 1 : 0);

    return 0;
}

unsigned int sys_open(const char * path, unsigned short mode);

long sys_close(unsigned int fd) {
    if (fd >= FD_LIMIT_PROCESS) return EBADF;
    file_descriptor_t * file = current_process->fds[fd];
    if (file == NULL) return EBADF;

    kassert(file->inode != NULL);
    
    if ((file->inode->instances == 0 || (file->inode->instances == 1 && file->inode->is_mountpoint)) || file->instances == 0) panic("Tried to close a file with 0 [process] instances\n");

    file->instances --;
    file->inode->instances --;
    current_process->fds[fd] = NULL;
    // call inode_cleanup() maybe
    return 0;
}

ssize_t sys_read(unsigned int fd, void * buf, size_t count) {
    unsigned long test = check_fd(fd);
    if (test != 0) return test;
    
    file_descriptor_t * file = current_process->fds[fd];

    switch (MAJOR(file->inode->device)) {
        case DEV_MAJ_TTY:
            return tty_read(file->inode->device, buf, count);
        default:
            kprintf("unknown dev major to read from (%d)...\n", MAJOR(file->inode->device));
            return EIO;
    }
}

ssize_t sys_write(unsigned int fd, const void * buf, size_t count) {
    unsigned long test = check_fd(fd);
    if (test != 0) return test;

    file_descriptor_t * file = current_process->fds[fd];


    switch (MAJOR(file->inode->device)) {
        case DEV_MAJ_TTY:
            return tty_write(file->inode->device, buf, count);
        default:
            kprintf("unknown dev major to write to (%d)...\n", MAJOR(file->inode->device));
            return EIO;
    }
}
