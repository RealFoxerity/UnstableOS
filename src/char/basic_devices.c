#include <stdlib.h> // the random function
#include "../../libc/src/include/UnstableOS/devs.h"
#include "dev_ops.h"
#include "fs/fs.h"
#include <string.h>

ssize_t null_pread(file_descriptor_t * file, void * buf, size_t count, off_t offset)        {return 0;}
ssize_t null_pwrite(file_descriptor_t * file, const void * buf, size_t count, off_t offset) {return count;}
static struct dev_operations null_ops = {
    .pread  = null_pread,
    .pwrite = null_pwrite,
};
ssize_t zero_pread(file_descriptor_t * file, void * buf, size_t count, off_t offset) {
    memset(buf, 0, count);
    return count;
}
ssize_t zero_pwrite(file_descriptor_t * file, const void * buf, size_t count, off_t offset) {return count;}
static struct dev_operations zero_ops = {
    .pread  = zero_pread,
    .pwrite = zero_pwrite,
};

ssize_t random_pread(file_descriptor_t * file, void * buf, size_t count, off_t offset) {
    for (size_t i = 0; i < count; i++) {
        ((unsigned char*)buf)[i] = rand() % 0xFF;
    }
    return count;
}
ssize_t random_pwrite(file_descriptor_t * file, const void * buf, size_t count, off_t offset) {return count;}
static struct dev_operations random_ops = {
    .pread  = random_pread,
    .pwrite = random_pwrite,
};


void dev_register_basic_devices() {
    dev_register_ops(GET_DEV(DEV_MAJ_MISC, DEV_MISC_NULL),   &null_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_MISC, DEV_MISC_ZERO),   &zero_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_MISC, DEV_MISC_RANDOM), &random_ops);
}