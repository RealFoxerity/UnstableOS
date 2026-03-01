#ifndef FS_H
#define FS_H
#include <stddef.h>
#include <stdint.h>

#include "../kernel_spinlock.h"
#include "../kernel.h"

#define FD_LIMIT_KERNEL 0x2000 // maximum amount of opened file descriptors kernel-wide, see FD_LIMIT_PROCESS
#define INODE_LIMIT_KERNEL 0x1000 // maximum amount of opened files kernel-wide
#define FS_LIMIT_KERNEL 0x100 // maximum amount of mounted file systems kernel-wide


#include "../../../libc/src/include/stdio.h" // for off_t, modes and I_* macros

// note that device fields here (except raw device inodes) are just for bookkeeping
// the intended solution is for file system drivers to create a file descriptor
// referring to the individual device, and then call sys_read/sys_write/sys_seek

struct inode_t {
    void * id; // unique identifier *for a given filesystem*

    unsigned short mode; // __I*

    size_t instances; // how many descriptors (and therefore processes) use this inode, 0 is considered an unused inode

    size_t size;

    struct superblock_t * backing_superblock; // used to lookup functions to use for i/o operations, same as next for "/"

    spinlock_t lock;

    char is_mountpoint; // if inode is a mountpoint, instances will be at least 1 to avoid clean, think of it as the superblock using it
    struct superblock_t * next_superblock; // pointer to the superblock structure mounted at this inode

    char is_raw_device; // this file represents a raw device, call the device functions rather than the superblock ones
    dev_t device;
} typedef inode_t;

struct {
    unsigned short mode; // O_*
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

    char is_mounted; // mark as unused so that we don't have to memset() at every unmount
} typedef superblock_t;

extern file_descriptor_t ** kernel_fds;
extern inode_t ** kernel_inodes;
extern superblock_t ** kernel_superblocks;

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
int open_raw_device(dev_t device, unsigned short mode); // locks file descriptor lock itself

void inode_change_mode(inode_t * inode, unsigned char new_mode);

int sys_open(const char * path, unsigned short flags, unsigned short mode);
int sys_close(int fd);
ssize_t sys_read(int fd, void * buf, size_t count);
ssize_t sys_write(int fd, const void * buf, size_t count);
off_t sys_seek(int fd, off_t off, int whence);


// because we want drivers' file objects to be file descriptors as well
// and we don't want them in user processes,
// we need separate functions for direct file_descriptor_t * operations
// this is theoretically more dangerous as we can't completely get rid of the fd and there could be UAF
// alternatively we could do message passing and deferring to the kernel but that's much harder
int close_file(file_descriptor_t * file); // primarily for closing on exit()
ssize_t read_file(file_descriptor_t * file, void * buf, size_t count);
ssize_t write_file(file_descriptor_t * file, const void * buf, size_t count);
off_t seek_file(file_descriptor_t * file, off_t off, int whence);

int sys_dup(int oldfd);
int sys_dup2(int oldfd, int newfd);

#define MOUNT_RDONLY 1

long sys_mount(const char * dev_path, const char * mount_path, unsigned char type, unsigned short options);

long mount_root(dev_t dev, unsigned char type, unsigned short options);

#endif