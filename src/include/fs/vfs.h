#ifndef VFS_H
#define VFS_H
#include <stddef.h>
#include "fs.h"


#define VFS_LOOKUP_NOTFOUND (NULL)
#define VFS_LOOKUP_ESCAPE ((inode_t*)-1) // out of scope of current fs
#define VFS_LOOKUP_NOTDIRECTORY ((inode_t*)-2) // /file/.

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

struct vfs_ops {
    int (*fs_init)   (superblock_t * sb); // called on mount, 0 = success
    int (*fs_deinit) (superblock_t * sb); // called on umount, 0 = success

    // it is assumed, that all specified paths are absolute,
    // and are "relative" to the root of the superblock

    // resolves symlink, returns string of target file, or NULL on error
    char * (*resolve_link)(superblock_t * sb, inode_t * link);

    // gets/creates the inode of a specified file
    // returns either inode pointer or VFS_LOOKUP*
    // in the case last/. returns the last pointer without altering anything
    // if last == NULL, last is assumed to be mounted /
    // implementations should accept empty string and "." to mean the current directory
    inode_t * (*lookup) (superblock_t * sb, inode_t * last, const char * pathname);
    int (*release)   (struct inode_t *); // closing of the very last instance of an inode

    ssize_t (*read)  (file_descriptor_t * fd, void * buf, size_t n);
    ssize_t (*write) (file_descriptor_t * fd, const void * buf, size_t n);
    off_t (*seek)    (file_descriptor_t * fd, off_t off, int whence);


    int (*unlink)    (superblock_t * sb, const char * pathname);
    int (*rmdir)     (superblock_t * sb, const char * pathname);

    //int (*readdir) (int fd, struct directory_entry * dirent, unsigned int count);
};

#define SUPPORTED_FS_COUNT 1
enum supported_filesystems {
    FS_TARFS,
};

extern const struct vfs_ops * fs_operations[SUPPORTED_FS_COUNT]; // defined in vfs.c, assigned in files belonging to individual fs

struct mount_tree {
    char * path; // the complete relative path from last mountpoint (/ -> mnt/drivea)
    inode_t * mountpoint; // to get next_superblock and for cleanup
    struct mount_tree * inner; // first mount instance in this mountpoint
    struct mount_tree * next; // next mount instance on this level
} typedef mount_tree;

extern spinlock_t mount_tree_lock;
extern struct mount_tree * root_mountpoint;

#endif