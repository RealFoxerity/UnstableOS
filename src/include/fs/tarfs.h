#ifndef TARFS_H
#define TARFS_H

#define USTAR_MAGIC "ustar\0" // should be this
#define USTAR_MAGIC_ALT "ustar " // gnu tar produces this, version is then " \0", idk
#define MAX_PATH_TAR 255
enum ustar_file_types {
    USTAR_NORMAL = '0',
    USTAR_HARD_LINK = '1',
    USTAR_SYMBOLIC_LINK = '2',
    USTAR_CHAR_DEV = '3',
    USTAR_BLOCK_DEV = '4',
    USTAR_DIRECTORY = '5',
    USTAR_PIPE = '6'
};

struct {
    char file_name[100];
    char modes[8];
    char owner_id[8];
    char group_id[8];
    char file_size[12]; // ascii octal representation
    char last_modified_time[12];
    char checksum[8];
    char type;
    char linked_file_name[100];
    char ustar_magic[6];
    char ustar_version[2]; // 00
    char owner_name[32];
    char group_name[32];
    char dev_major[8];
    char dev_minor[8];
    char filename_prefix[155];
    char __pad[12]; // the structure is 512 bytes
} __attribute__((packed)) typedef ustar_hdr;

void tar_debug_print_files(int tar_fd);

#include "fs.h"

// these functions aren't meant to be called directly
// they are called from the vfs

int tar_load_fs(superblock_t * sb); // prepares the superblock
int tar_unload_fs(superblock_t * sb); // frees the internal lookup structure
off_t tarfs_seek(file_descriptor_t * fd, off_t off, int whence);
inode_t * tarfs_lookup(superblock_t * sb, inode_t * last, const char * pathname);
ssize_t tarfs_read(file_descriptor_t * fd, void * buf, size_t n);

#include "vfs.h"
extern const struct vfs_ops tar_op;
#endif