#ifndef FS_H
#define FS_H
#include <stddef.h>
#include <stdint.h>

#include "../kernel_spinlock.h"
#include "../kernel.h"

#define FD_LIMIT_KERNEL 0x2000 // maximum amount of opened file descriptors kernel-wide, see FD_LIMIT_PROCESS
#define INODE_LIMIT_KERNEL 0x1000 // maximum amount of opened files kernel-wide
#define FS_LIMIT_KERNEL 0x100 // maximum amount of mounted file systems kernel-wide

#define __ITMODE_MASK       0xF000
#define __ITMODE_REG        0x1000
#define __ITMODE_DIR        0x2000
#define __ITMODE_BLK        0x4000
#define __ITMODE_CHAR       0x8000

#define __IPMODE_MASK       0x0FFF
#define __IPMODE_O_READ     0x0001
#define __IPMODE_O_WRITE    0x0002
#define __IPMODE_O_EXEC     0x0004

#define __IPMODE_G_READ     0x0010
#define __IPMODE_G_WRITE    0x0020
#define __IPMODE_G_EXEC     0x0040

#define __IPMODE_U_READ     0x0100
#define __IPMODE_U_WRITE    0x0200
#define __IPMODE_U_EXEC     0x0400

#define I_ISREG(mode) (((mode) & __ITMODE_MASK) == __ITMODE_REG)
#define I_ISDIR(mode) (((mode) & __ITMODE_MASK) == __ITMODE_DIR)
#define I_ISBLK(mode) (((mode) & __ITMODE_MASK) == __ITMODE_BLK)
#define I_ISCHAR(mode) (((mode) & __ITMODE_MASK) == __ITMODE_CHAR)

#define O_RDONLY 1
#define O_WRONLY 2
#define O_RDWR 3

struct inode_t {
    size_t id; // unique identifier *for a given filesystem*

    unsigned short mode; // O_*

    size_t instances; // how many descriptors (and therefore processes) use this inode, 0 is considered an unused inode
    size_t hardlinks; // how many hardlinks point to this file, including the file itself

    size_t size;

    dev_t device; // device backing this inode, basically the file system, for superblock lookup

    spinlock_t lock;

    char is_mountpoint; // if inode is a mountpoint, instances will be at least 1 to avoid clean, think of it as the superblock using it
    char is_raw_device; 
    // whether the inode is a direct reference to a device - we shouldn't perform superblock lookup - device then holds 
    // the actual raw dev_t for the destination device rather than the device holding a file resembling a device
    // used for backing superblocks with real devices, memdisks, and initial console for the kernel task

} typedef inode_t;

struct {
    unsigned short mode;
    unsigned short flags;

    size_t instances; // how many processes have this descriptor opened (for fork() duplication)

    inode_t * inode;

    size_t off;
} typedef file_descriptor_t; // userspace will use an int as an index to per-process array of file_descriptor_t pointers

struct superblock_t;
struct vfs_ops {
    ssize_t (*read) (struct superblock_t * superblock , size_t id, size_t off, size_t n, char * dest);
    ssize_t (*write)(struct superblock_t * superblock , size_t id, size_t off, size_t n, const char * src);
    ssize_t (*open) (struct superblock_t * superblock , const char * pathname);
    ssize_t (*close)(struct superblock_t * superblock , size_t id);
};

struct superblock_t {
    dev_t device;
    unsigned char fs_type; // no way we support >256 file systems

    spinlock_t lock;
    
    unsigned short mount_options;

    inode_t * mountpoint;

    struct vfs_ops funcs;

    char is_mounted; // mark as unused so that we don't have to memset() at every unmount
} typedef superblock_t;

extern file_descriptor_t ** kernel_fds;
extern inode_t ** kernel_inodes;
extern superblock_t * kernel_superblocks[FS_LIMIT_KERNEL];

void init_fds();
void init_inodes();

extern spinlock_t kernel_inode_lock;
extern spinlock_t kernel_fd_lock;
extern spinlock_t kernel_superblock_lock;

inode_t * get_free_inode();
file_descriptor_t * get_free_fd();

ssize_t sys_read(unsigned int fd, void * buf, size_t count);
ssize_t sys_write(unsigned int fd, const void * buf, size_t count);

long sys_mount(const char * dev_path, const char * mount_path, unsigned short options);
#endif