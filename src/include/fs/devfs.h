#ifndef DEVFS_H
#define DEVFS_H
#include "fs.h"
#include "vfs.h"


extern const struct vfs_ops devfs_op;
inode_t * devfs_lookup(superblock_t * sb, inode_t * last, const char * pathname);
off_t devfs_seek(file_descriptor_t * fd, off_t off, int whence);
ssize_t devfs_readdir(file_descriptor_t * fd, struct dirent * dent, size_t dent_size);
int devfs_stat(inode_t * file, struct stat * buf);
#endif
