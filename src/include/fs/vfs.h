#ifndef VFS_H
#define VFS_H
#include <stddef.h>
#include "fs.h"
#include "../../../libc/src/include/sys/stat.h"

#define VFS_LOOKUP_ESCAPE 1

#define PATH_PARENT ".."
#define PATH_CURRENT "."

/*
posix says // is implementation defined so the idea is to have special functionality there
nothing is yet implemented but i am thinking of having devices and network sockets there
so
//disk1
//net/tcp/1.2.3.4/1111
...
*/
#define PATH_METADIR "//"

#include <dirent.h>
struct vfs_ops {
    int (*fs_init)   (superblock_t * sb); // called on mount, 0 = success
    int (*fs_deinit) (superblock_t * sb); // called on umount, 0 = success, TODO: error handling in umount

    // resolves symlink, returns string of target file, or NULL on error
    char * (*resolve_link)(superblock_t * sb, inode_t * link);

    // gets/creates the inode of a specified file
    // returns standard errnos or VFS_LOOKUP_ESCAPE on root/..
    // in the case last/. returns the last pointer without altering anything
    // if last == NULL, last is assumed to be mounted /
    // implementations should accept "." to mean the current directory
    int (*lookup)   (superblock_t * sb, inode_t * last, const char * pathname, inode_t ** inode_out);
    int (*release)   (inode_t *); // closing of the very last instance of an inode

    int (*stat)      (inode_t * file, struct stat * buf);

    ssize_t (*pread) (file_descriptor_t * fd, void * buf, size_t n, off_t offset);
    ssize_t (*pwrite)(file_descriptor_t * fd, const void * buf, size_t n, off_t offset);
    off_t (*seek)    (file_descriptor_t * fd, off_t off, int whence);


    // doubles as rmdir if file is a directory
    // implementations are required to do all locking needed to not race on unlink and open/lookup
    // MT safety is guaranteed by sys_unlinkat, don't need to lock the inode separately
    int (*unlink)    (inode_t * file);
    int (*trunc)     (inode_t * file, off_t length);

    inode_t *(*creat)(inode_t * parent, const char * pathname, mode_t mode);
    inode_t *(*mkdir)(inode_t * parent, const char * pathname, mode_t mode);

    // note: fd offset 0 is considered the "." folder to simplify userspace rewinddir()
    // the function implementation is required to set fd->off to new offset
    // do not just read fd->off, use offset, fd->off is prone to races
    // always check valid offsets, seekdir isn't passed to vfs seek
    ssize_t (*readdir) (file_descriptor_t * fd, struct dirent * dent, size_t dent_size, off_t offset);
    //off_t(*telldir)(file_descriptor_t * fd); // handled via normal seek()
    //off_t(*seekdir)(file_descriptor_t * fd);
    //void(*rewinddir)(file_descriptor_t * fd);
};

#include <UnstableOS/mount.h>
extern const struct vfs_ops * fs_operations[SUPPORTED_FS_COUNT]; // defined in vfs.c, assigned in files belonging to individual fs

// this entire structure is basically just for record keeping
struct mount_tree {
    char * path; // the complete relative path from last mountpoint (/ -> mnt/drivea)
    inode_t * mountpoint; // to get next_superblock and for cleanup
    struct mount_tree * inner; // first mount instance in this mountpoint
    struct mount_tree * next; // next mount instance on this level
} typedef mount_tree;

extern spinlock_t mount_tree_lock;
extern struct mount_tree * root_mountpoint;

#endif