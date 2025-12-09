#include "../include/fs/fs.h"
#include "../include/kernel_sched.h"
#include "../include/errno.h"

file_descriptor_t * kernel_fds[FD_LIMIT_KERNEL] = {0};

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
    current_thread->status = SCHED_UNINTERR_SLEEP;
    return -1;
}

ssize_t sys_write(unsigned int fd, const void * buf, size_t count) {
    if (fd >= FD_LIMIT_PROCESS) return EBADF;
    file_descriptor_t * file = current_process->fds[fd];
    if (file == NULL) return EBADF;

    kassert(file->instances > 0);
    kassert(file->inode != NULL);
    kassert(file->inode->instances > (file->inode->is_mountpoint)? 1 : 0);

    if (fd == STDOUT || fd == STDERR) {
        return count;
    }
    return -1;
}
