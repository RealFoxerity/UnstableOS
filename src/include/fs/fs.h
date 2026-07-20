#ifndef FS_H
#define FS_H
#include <stddef.h>
#include <stdint.h>

#include "../kernel_spinlock.h"
#include "../kernel.h"

#define FD_LIMIT_KERNEL 0x2000 // maximum amount of opened file descriptors kernel-wide, see FD_LIMIT_PROCESS
#define INODE_LIMIT_KERNEL 0x1000 // maximum amount of opened files kernel-wide
#define FS_LIMIT_KERNEL 0x100 // maximum amount of mounted file systems kernel-wide


#include <unistd.h> // for off_t, and seek modes
#include <fcntl.h> // for O_* and I_* macros


struct pipe;

struct inode_t {
    ino_t id; // unique identifier *for a given filesystem*

    mode_t mode; // __I*

    uid_t uid;
    gid_t gid;

    nlink_t nlink;

    time_t btime, ctime, mtime, atime;

    size_t instances; // how many descriptors (and therefore processes) use this inode, 0 is considered an unused inode

    off_t size;
    blksize_t io_block_size;

    struct superblock_t * backing_superblock; // used to lookup functions to use for i/o operations, same as next for "/"

    spinlock_t lock; // never try to lock with interrupts enabled, will deadlock scheduler on process destroying

    char is_mountpoint; // if inode is a mountpoint, instances will be at least 1 to avoid clean, think of it as the superblock using it
    struct superblock_t * next_superblock; // pointer to the superblock structure mounted at this inode

    union {
        struct {
            dev_t device; // if S_ISBLK(mode) | S_ISCHR(mode)
            char dev_opened;
        };
        struct pipe * pipe; // if S_ISFIFO(mode); extra field so that named pipes can be more easily implemented
    };
} typedef inode_t;

struct {
    unsigned short flags;

    size_t instances; // how many processes have this descriptor opened (for fork()/dup*() duplication)

    inode_t * inode;

    off_t off;

    rw_spinlock_t access_lock; // so that thread io operations are atomic
} typedef file_descriptor_t; // userspace will use an int as an index to per-process array of file_descriptor_t pointers

// intended way of accessing superblocks and
struct superblock_t {
    dev_t device; // bookkeeping
    file_descriptor_t * fd; // file descriptor belonging to the underlying device

    size_t instances; // how many inodes have this as their backing superblock

    unsigned char fs_type; // no way we support >256 file systems, bookkeeping, alternatively, for kasserts in fs drivers

    spinlock_t lock;

    unsigned short mount_options;

    const struct vfs_ops * funcs;
    void * data; // for internal structures of individual file system drivers

    inode_t * mountpoint; // the mountpoint in the previous fs, extremely useful for traversing paths

    char is_mounted; // mark as unused so that we don't have to memset() at every unmount
} typedef superblock_t;

extern file_descriptor_t ** kernel_fds;
extern inode_t ** kernel_inodes;
extern superblock_t ** kernel_superblocks;

/* Intentionally here, so that when kernel_sched_queues.h includes kernel_sched.h
 * file_descriptor_t and inode_t is already defined
 */

#include <limits.h>
#include "../kernel_sched_queues.h"

struct pipe {
    spinlock_t pipe_lock;
    struct thread_queue read_queue, write_queue;
    unsigned char pipe_fifo[PIPE_BUF];
    size_t head, tail;

    size_t readers; // SIGPIPE
    size_t writers; // EOF on read
};

void init_fds();
void init_inodes();
void init_superblocks();

extern spinlock_t kernel_inode_lock;
extern spinlock_t kernel_fd_lock;
extern spinlock_t kernel_superblock_lock;

inode_t * get_free_inode(); // lock inode lock beforehand, sets instances to 1
file_descriptor_t * get_free_fd(); // lock file descriptor lock beforehand, sets instances to 1
superblock_t * get_free_superblock(); // locks superblock lock itself, sets is_mounted to 1

inode_t * get_inode_raw_device(dev_t device); // get an existing structure for a given raw device, NULL if not open
inode_t * __get_inode_raw_device(dev_t device); // the same but caller has to lock kernel_inode_lock

// locks inode lock itself, finds an existing structure and increments, or creates one and sets instances to 1
// meant for kernel space before vfs access
// call sparingly! leads to duplicate dev inodes
long inode_from_device(dev_t device, inode_t ** inode_out);

// considering there can't (shouldn't) be 2 inodes with the same id and sb pointer,
// this looks up such an inode from the kernel's inode list, returning NULL when it can't
// locks inode lock itself
inode_t * get_inode(superblock_t * sb, off_t inode_number);
// exactly the same but increases instances if found and creates if not
long register_inode(const inode_t * inode, inode_t ** inode_out, unsigned short dev_flags);
void close_inode(inode_t * inode);
int open_raw_device(dev_t device, unsigned short flags, file_descriptor_t ** file_out); // locks file descriptor lock itself
int open_raw_device_fd(dev_t device, unsigned short flags); // locks file descriptor lock itself

void inode_change_mode(inode_t * inode, unsigned short new_mode);

// puts a new fd into the current process
int get_fd_from_inode(inode_t * inode, unsigned short flags);

int sys_openat(int fd, const char * path, unsigned short flags, mode_t mode);
// the kernel function itself
int openat_inode(inode_t * base, const char * path, unsigned short flags, mode_t mode, inode_t ** out, char trusted_path);

int sys_chdir(const char * path);
int sys_chroot(const char * path);

int sys_fstat(int fd, struct stat * buf);
int sys_fstatat(int fd, const char * __restrict path, struct stat * __restrict buf, int flags);

int sys_close(int fd);
ssize_t sys_read(int fd, void * buf, size_t count);
ssize_t sys_write(int fd, const void * buf, size_t count);
ssize_t sys_pread(int fd, void * buf, size_t count, off_t offset);
ssize_t sys_pwrite(int fd, const void * buf, size_t count, off_t offset);
off_t sys_seek(int fd, off_t off, int whence);

int sys_trunc(int fd, off_t length);

long sys_ioctl(int fd, unsigned long request, void * arg);

long sys_fcntl(int fd, int cmd, long arg);

#include <dirent.h>
ssize_t sys_readdir(int fd, struct dirent * dent, size_t dent_size);

// because we want drivers' file objects to be file descriptors as well
// and we don't want them in user processes,
// we need separate functions for direct file_descriptor_t * operations
// this is theoretically more dangerous as we can't completely get rid of the fd and there could be UAF
// alternatively we could do message passing and deferring to the kernel but that's much harder
int close_file(file_descriptor_t * file); // primarily for closing on exit()
// when we know for 100% that the inode isn't used anywhere in the running process
// (for example the scheduler when destroying an application)
// so that we don't have to needlessly wait
int close_file_forced(file_descriptor_t * file);

ssize_t read_file(file_descriptor_t * file, void * buf, size_t count);
ssize_t write_file(file_descriptor_t * file, const void * buf, size_t count);
ssize_t pread_file(file_descriptor_t * file, void * buf, size_t count, off_t offset);
ssize_t pwrite_file(file_descriptor_t * file, const void * buf, size_t count, off_t offset);

off_t seek_file(file_descriptor_t * file, off_t off, int whence);
long fcntl_file(file_descriptor_t * file, int cmd, long arg);

ssize_t ps2_mouse_pread(file_descriptor_t * file, void * buf, size_t n, off_t offset);

// here because we need to access file_descriptor_t
int sys_pipe(int fildes[2], int flags);
ssize_t pipe_write(const file_descriptor_t * file, const void * s, size_t n);
ssize_t pipe_read(const file_descriptor_t * file, void * s, size_t n);

long dup_file(file_descriptor_t * old_file, int startfd, int flags); // primarily for fcntl
int sys_dup(int oldfd);
int sys_dup3(int oldfd, int newfd, int flags);
int sys_unlinkat(int fd, const char *path, int flags);
int sys_renameat(int oldfd, const char * old, int newfd, const char * new);

#include <UnstableOS/mount.h>
long mount_dev(dev_t dev, inode_t * mount_point, unsigned char type, unsigned short options);
long mount_root(dev_t dev, unsigned char type, unsigned short options);
long sys_mount(const char * dev_path, const char * mount_path, unsigned char type, unsigned short options);
long sys_umount(const char * mount_path);

off_t generic_seek(file_descriptor_t *file, off_t off, int whence, off_t max_off);

int utimes_inode(inode_t * inode, struct timespec atime, struct timespec mtime, struct timespec ctime);
int sys_utimensat(int fd, const char *path, const struct timespec times[2], int flag);

#endif
