#include "fs/fs.h"
#include "fs/vfs.h"
#include "dev_ops.h"
#include "kernel_sched.h"
#include "errno.h"
#include <string.h>
#include <sys/stat.h>
#include "kernel_tty_io.h"
#include "block/memdisk.h"

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

    unsigned char fd_flags = (flags & (O_CLOEXEC | O_CLOFORK)) >> 12;
    flags &= ~(O_CLOEXEC | O_CLOFORK);

    file->inode = inode;
    file->flags = flags;
    current_process->fd_flags[fd] = fd_flags;
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
    inode_t * inode = file->inode; // to not race on freeing
    unsigned short old_flags = file->flags;

    if (__atomic_sub_fetch(&file->instances, 1, __ATOMIC_RELAXED) == 0) {
        // to have the correct SIGPIPE behavior
        // has to be done here, because the inode doesn't carry flags
        if (S_ISFIFO(file->inode->mode)) {
            if (old_flags & O_RDONLY)
                __atomic_sub_fetch(&inode->pipe->readers, 1, __ATOMIC_RELAXED);
            else
                __atomic_sub_fetch(&inode->pipe->writers, 1, __ATOMIC_RELAXED);
            thread_queue_unblock_all_nonreentrant(&inode->pipe->read_queue);
            thread_queue_unblock_all_nonreentrant(&inode->pipe->write_queue);
        }

        close_inode(inode);
    }
    return 0;
}

int close_file(file_descriptor_t * file) {
    // have to be read
    // e.g. pipe that's already reading on one thread would cause deadlock
    rw_spinlock_acquire_read(&file->access_lock);
    spinlock_acquire(&kernel_fd_lock);
    close_file_forced(file);
    // call inode_cleanup() maybe
    rw_spinlock_release_read(&file->access_lock); // has to be before kernel_fd_lock, otherwise UAF
    spinlock_release(&kernel_fd_lock);
    return 0;
}

ssize_t pread_file(file_descriptor_t *file, void *buf, size_t count, off_t offset) {
    if (offset < 0) return -EINVAL;

    int test = check_file(file);
    if (test != 0) return test;
    if (file->flags & O_SEARCH) return -EBADF;

    if (S_ISDIR(file->inode->mode)) return -EISDIR;
    if (!(file->flags & O_RDONLY)) return -EINVAL;
    if (count == 0) return 0;

#ifdef E2BIG_ON_2G
    if (count > SSIZE_MAX) return -E2BIG;
#else
    if (count > SSIZE_MAX) count = SSIZE_MAX;
#endif

    if ((S_ISFIFO(file->inode->mode) || S_ISCHR(file->inode->mode)) && offset != 0) return -ESPIPE;

    ssize_t ret = 0;
    rw_spinlock_acquire_read(&file->access_lock);
    if (S_ISFIFO(file->inode->mode)) {
        ret = pipe_read(file, buf, count);
    } else if (!(S_ISBLK(file->inode->mode) || S_ISCHR(file->inode->mode))) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (file->inode->backing_superblock->funcs->pread == NULL)
            ret = -EINVAL;
        else
            ret = file->inode->backing_superblock->funcs->pread(file, buf, count, offset);
    } else {
        ret = pread_dev(file, buf, count, offset);
    }
    rw_spinlock_release_read(&file->access_lock);
    return ret;
}

ssize_t read_file(file_descriptor_t *file, void *buf, size_t count) {
    int test = check_file(file);
    if (test != 0) return test;

    off_t old_off = file->off;
    if (S_ISFIFO(file->inode->mode))
        old_off = 0;

    ssize_t ret = pread_file(file, buf, count, old_off);
    if (ret < 0 || S_ISFIFO(file->inode->mode) || S_ISCHR(file->inode->mode)) return ret;

    old_off += ret;

    // this would be lockless,
    // but since off_t is ull,
    // this requires a cmpxchg8b,
    // which is unfortunately from i586 onwards
    // __atomic_store_n(&file->off, old_off, __ATOMIC_RELAXED);

    rw_spinlock_acquire_write(&file->access_lock);
    file->off = old_off;
    rw_spinlock_release_write(&file->access_lock);
    return ret;
}

ssize_t pwrite_file(file_descriptor_t *file, const void *buf, size_t count, off_t offset) {
    if (offset < 0) return -EINVAL;

    int test = check_file(file);
    if (test != 0) return test;
    if (file->flags & O_SEARCH) return -EBADF;

    if (S_ISDIR(file->inode->mode)) return -EISDIR;
    if (!(file->flags & O_WRONLY))
        return -EINVAL;

#ifdef E2BIG_ON_2G
    if (count > SSIZE_MAX) return -E2BIG;
#else
    if (count > SSIZE_MAX) count = SSIZE_MAX;
#endif
    if (count == 0) return 0;

    if ((S_ISFIFO(file->inode->mode) || S_ISCHR(file->inode->mode)) && offset != 0) return -ESPIPE;

    ssize_t ret = 0;
    rw_spinlock_acquire_read(&file->access_lock);

    if (S_ISFIFO(file->inode->mode)) {
        ret = pipe_write(file, buf, count);
    } else if (!(S_ISBLK(file->inode->mode) || S_ISCHR(file->inode->mode))) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (!(file->inode->backing_superblock->mount_options & MOUNT_RDONLY)) {
            kprintf("Warning: File descriptor marked writable on read-only fs, readjusting...\n");
            file->flags ^= O_WRONLY;
            rw_spinlock_release_read(&file->access_lock);
            return -EINVAL;
        }
        if (file->inode->backing_superblock->funcs->pwrite == NULL)
            ret = -EINVAL;
        else {
            // O_APPEND doesn't make sense in any other case, so that's why here
            // offset = ...seek just in case we race to the seek
            // the file->off isn't important anyway
            if (file->flags & O_APPEND &&
                file->inode->backing_superblock->funcs->seek != NULL)
                    offset = file->inode->backing_superblock->funcs->seek(file, 0, SEEK_END);

            ret = file->inode->backing_superblock->funcs->pwrite(file, buf, count, offset);
        }
    } else {
        ret = pwrite_dev(file, buf, count, offset);
    }

    rw_spinlock_release_read(&file->access_lock);
    return ret;
}

ssize_t write_file(file_descriptor_t *file, const void *buf, size_t count) {
    int test = check_file(file);
    if (test != 0) return test;

    off_t old_off = file->off;
    if (S_ISFIFO(file->inode->mode))
        old_off = 0;

    ssize_t ret = pwrite_file(file, buf, count, old_off);
    if (ret < 0 || S_ISFIFO(file->inode->mode) || S_ISCHR(file->inode->mode)) return ret;

    old_off += ret;
    // see comment in read_file
    // __atomic_store_n(&file->off, old_off, __ATOMIC_RELAXED);
    rw_spinlock_acquire_write(&file->access_lock);
    file->off = old_off;
    rw_spinlock_release_write(&file->access_lock);
    return ret;
}

off_t seek_file(file_descriptor_t * file, off_t off, int whence) {
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

    off_t ret = 0;
    if (S_ISFIFO(file->inode->mode)) { return -ESPIPE; }
    rw_spinlock_acquire_read(&file->access_lock);
    if (!(S_ISBLK(file->inode->mode) || S_ISCHR(file->inode->mode))) {
        kassert(file->inode->backing_superblock);
        kassert(file->inode->backing_superblock->funcs);
        if (file->inode->backing_superblock->funcs->seek == NULL)
            ret = -EINVAL;
        else
            ret = file->inode->backing_superblock->funcs->seek(file, off, whence);
    } else if (S_ISBLK(file->inode->mode) || S_ISCHR(file->inode->mode)) {
        ret = seek_dev(file, off, whence);
    } else {
        ret = -ESPIPE; // assuming file is not seekable
    }
    rw_spinlock_release_read(&file->access_lock);
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

ssize_t sys_pread(int fd, void * buf, size_t count, off_t offset) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];

    return pread_file(file, buf, count, offset);
}

ssize_t sys_pwrite(int fd, const void * buf, size_t count, off_t offset) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];

    return pwrite_file(file, buf, count, offset);
}

off_t sys_seek(int fd, off_t off, int whence) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];

    return seek_file(file, off, whence);
}

long dup_file(file_descriptor_t * old_file, int startfd, int flags) {
    if (startfd < 0 || startfd >= FD_LIMIT_PROCESS) return -EINVAL;

    spinlock_acquire(&kernel_fd_lock);
    if (old_file == NULL) {
        spinlock_release(&kernel_fd_lock);
        return -EBADF;
    }

    int fd = -1;
    for (int i = startfd; i < FD_LIMIT_PROCESS; i++) {
        if (!current_process->fds[i]) {
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        spinlock_release(&kernel_fd_lock);
        return -EMFILE;
    }

    __atomic_add_fetch(&old_file->instances, 1, __ATOMIC_RELAXED);

    current_process->fd_flags[fd] = 0;
    current_process->fds[fd] = old_file;

    spinlock_release(&kernel_fd_lock);

    return fd;
}
int sys_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * old_file = current_process->fds[oldfd];

    return dup_file(old_file, 0, 0);
}

int sys_dup3(int oldfd, int newfd, int flags) {
    if (oldfd < 0 || oldfd >= FD_LIMIT_PROCESS) return -EBADF;
    if (newfd < 0 || newfd >= FD_LIMIT_PROCESS) return -EBADF;

    if (oldfd == newfd && flags != -1) return -EINVAL; // dup3
    if (oldfd == newfd) return oldfd;

    spinlock_acquire(&kernel_fd_lock);
    if (current_process->fds[oldfd] == NULL) {
        spinlock_release(&kernel_fd_lock);
        return -EBADF;
    }

    __atomic_add_fetch(&current_process->fds[oldfd]->instances, 1, __ATOMIC_RELAXED);

    if (current_process->fds[newfd] != NULL) { // close the fd
        rw_spinlock_acquire_read(&current_process->fds[newfd]->access_lock);
        close_file_forced(current_process->fds[newfd]);
        rw_spinlock_release_read(&current_process->fds[newfd]->access_lock);
        current_process->fds[newfd] = NULL;
    }

    if (flags == -1) flags = 0;

    flags &= O_CLOEXEC | O_CLOFORK;

    current_process->fd_flags[newfd] = flags >> 12; // 0x1000 -> 1
    current_process->fds[newfd] = current_process->fds[oldfd];

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

    // can't lock because i486 doesn't have 64 bit atomics,
    // requiring us to lock writable inside readdir for setting offset
    //rw_spinlock_acquire_read(&file->access_lock);
    ssize_t ret = file->inode->backing_superblock->funcs->readdir(file, dent, dent_size, file->off);
    //rw_spinlock_release_read(&file->access_lock);

    return ret;
}

int stat_inode(inode_t * inode, struct stat * buf) {
    kassert(buf);
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

long ioctl_file(file_descriptor_t * file, unsigned long command, void * arg) {
    int test = check_file(file);
    if (test != 0) return test;

    rw_spinlock_acquire_read(&file->access_lock);
    long ret = ioctl_dev(file, command, arg);
    rw_spinlock_release_read(&file->access_lock);
    return ret;
}

long sys_ioctl(int fd, unsigned long request, void * arg) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];

    return ioctl_file(file, request, arg);
}

long fcntl_file(file_descriptor_t * file, int cmd, long arg) {
    int test = check_file(file);
    if (test != 0) return test;
    if (cmd < 0) return -EINVAL;
    if (arg < 0) return -EINVAL;

    long ret = 0;
    rw_spinlock_acquire_write(&file->access_lock);

    switch (cmd) {
        case F_DUPFD:
            ret = dup_file(file, arg, 0);
            break;
        case F_DUPFD_CLOEXEC:
            ret = dup_file(file, arg, O_CLOEXEC);
            break;
        case F_DUPFD_CLOFORK:
            ret = dup_file(file, arg, O_CLOFORK);
            break;
        case F_GETFL:
            ret = file->flags & (O_SYNC | O_APPEND);
            break;
        case F_SETFL:
            file->flags |= arg & (O_SYNC | O_APPEND);
            break;
        default: ret = -EINVAL;
    }

    rw_spinlock_release_write(&file->access_lock);

    return ret;
}

long sys_fcntl(int fd, int cmd, long arg) {
    if (fd < 0 || fd >= FD_LIMIT_PROCESS) return -EBADF;

    file_descriptor_t * file = current_process->fds[fd];
    int test = check_file(file);
    if (test != 0) return test;
    switch (cmd) {
        case F_GETFD:
            return current_process->fd_flags[fd] << 12;
        case F_SETFD:
            arg &= O_CLOEXEC | O_CLOFORK;
            current_process->fd_flags[fd] = arg >> 12;
            return 0;
        default:
            return fcntl_file(file, cmd, arg);
    }
}


off_t generic_seek(file_descriptor_t *file, off_t off, int whence, off_t max_off) {
    kassert(file);

    off_t old_off = file->off;
    switch (whence) {
        case SEEK_SET:
            if (off < 0) return -EINVAL;
            if (off > max_off) return -EINVAL;
            return (file->off = off);
        case SEEK_CUR:
            if (old_off + off > old_off && off < 0) return -EINVAL; // underflow - negative offset
            if (old_off + off < old_off && off > 0) return -E2BIG; // overflow

            if (old_off + off > max_off) return -EINVAL;
            return (file->off = old_off + off);
        case SEEK_END:
            if (off > 0) return -EINVAL;
            if (off == 0) return (file->off = max_off);
            if (-off <= old_off) return (file->off = old_off - off);
            return -EINVAL; // negative offset
        default:
            return -EINVAL;
    }
}