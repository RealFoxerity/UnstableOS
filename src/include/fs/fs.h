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
    void * id; // unique identifier *for a given filesystem*

    mode_t mode; // __I*

    size_t instances; // how many descriptors (and therefore processes) use this inode, 0 is considered an unused inode

    size_t size;

    struct superblock_t * backing_superblock; // used to lookup functions to use for i/o operations, same as next for "/"

    spinlock_t lock;

    char is_mountpoint; // if inode is a mountpoint, instances will be at least 1 to avoid clean, think of it as the superblock using it
    struct superblock_t * next_superblock; // pointer to the superblock structure mounted at this inode

    union {
        dev_t device; // if S_ISBLK(mode) | S_ISCHR(mode)
        struct pipe * pipe; // if S_ISFIFO(mode); extra field so that named pipes can be more easily implemented
    };
} typedef inode_t;

struct {
    unsigned short flags;

    size_t instances; // how many processes have this descriptor opened (for fork()/dup*() duplication)

    inode_t * inode;

    size_t off; // even though we return off_t, which is signed, we use unsigned ints for the internal offset so that we can overcome 31 bits

    spinlock_t access_lock; // so that thread io operations are atomic
} typedef file_descriptor_t; // userspace will use an int as an index to per-process array of file_descriptor_t pointers

// intended way of accessing superblocks and
struct superblock_t {
    dev_t device; // bookkeeping
    file_descriptor_t * fd; // file descriptor belonging to the underlying device

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

#include <sys/limits.h>
#include "../kernel_sched_queues.h"

struct pipe {
    spinlock_t pipe_lock;
    struct thread_queue read_queue, write_queue;
    unsigned char pipe_fifo[PIPE_BUF];
    size_t head, tail;
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
inode_t * inode_from_device(dev_t device); // locks inode lock itself, finds an existing structure and increments, or creates one and sets instances to 1

// considering there can't (shouldn't) be 2 inodes with the same id and sb pointer,
// this looks up such an inode from the kernel's inode list, returning NULL when it can't
// locks inode lock itself
inode_t * get_inode(superblock_t * sb, void * inode_number);
// exactly the same but increases instances if found and creates if not
inode_t * create_inode(superblock_t * sb, void * inode_number);
void close_inode(inode_t * inode);
int open_raw_device(dev_t device, unsigned short flags); // locks file descriptor lock itself

void inode_change_mode(inode_t * inode, unsigned short new_mode);

// puts a new fd into the current process
int get_fd_from_inode(inode_t * inode, unsigned short flags);

int sys_open(const char * path, unsigned short flags, unsigned short mode);
int sys_openat(int fd, const char * path, unsigned short flags, unsigned short mode);
// the kernel function itself
int openat_inode(inode_t * base, const char * path, unsigned short flags, unsigned short mode, inode_t ** out);

int sys_chdir(const char * path);
int sys_chroot(const char * path);

int sys_fstat(int fd, struct stat * buf);
int sys_fstatat(int fd, const char * __restrict path, struct stat * __restrict buf, int flags);

int sys_close(int fd);
ssize_t sys_read(int fd, void * buf, size_t count);
ssize_t sys_write(int fd, const void * buf, size_t count);
off_t sys_seek(int fd, off_t off, int whence);

#include <dirent.h>
ssize_t sys_readdir(int fd, struct dirent * dent, size_t dent_size);

// because we want drivers' file objects to be file descriptors as well
// and we don't want them in user processes,
// we need separate functions for direct file_descriptor_t * operations
// this is theoretically more dangerous as we can't completely get rid of the fd and there could be UAF
// alternatively we could do message passing and deferring to the kernel but that's much harder
int close_file(file_descriptor_t * file); // primarily for closing on exit()
ssize_t read_file(file_descriptor_t * file, void * buf, size_t count);
ssize_t write_file(file_descriptor_t * file, const void * buf, size_t count);
off_t seek_file(file_descriptor_t * file, off_t off, int whence);

// here because we need to access file_descriptor_t
int sys_pipe(int fildes[2]);
ssize_t pipe_write(const file_descriptor_t * file, const void * s, size_t n);
ssize_t pipe_read(const file_descriptor_t * file, void * s, size_t n);

int sys_dup(int oldfd);
int sys_dup2(int oldfd, int newfd);

#define MOUNT_RDONLY 1

long sys_mount(const char * dev_path, const char * mount_path, unsigned char type, unsigned short options);

long mount_root(dev_t dev, unsigned char type, unsigned short options);

#endif