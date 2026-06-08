#ifndef DEV_OPS_H
#define DEV_OPS_H

#include "fs/fs.h"

// ioctl arg memory cannot be checked in the syscall dispatcher
// each ioctl implementation must check that the memory area is valid
// for example with paging_check_address_range()
struct dev_operations {
    ssize_t (*pread) (file_descriptor_t *file, void *buf, size_t count, off_t offset);
    ssize_t (*pwrite)(file_descriptor_t *file, const void *buf, size_t count, off_t pread);
    off_t   (*seek) (file_descriptor_t *file, off_t offset, int whence);
    long    (*ioctl)(file_descriptor_t *file, unsigned long request, void * arg);
};

ssize_t pread_dev(file_descriptor_t *file, void *buf, size_t count, off_t offset);
ssize_t pwrite_dev(file_descriptor_t *file, const void *buf, size_t count, off_t offset);
off_t seek_dev(file_descriptor_t * file, off_t offset, int whence);
long ioctl_dev(file_descriptor_t *file, unsigned long request, void * arg);

// if one already exists, it gets overwritten!
void dev_register_ops(dev_t dev, const struct dev_operations * dev_ops);
struct dev_operations dev_ops_lookup(dev_t dev);
void dev_ops_remove(dev_t dev);

// 32 chars should be plenty
char * dev2string(dev_t device, char * buf_out);
#endif
