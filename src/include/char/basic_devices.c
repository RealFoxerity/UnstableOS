#include <stdlib.h> // the random function
#include "devs.h"
#include "dev_ops.h"
#include "fs/fs.h"
#include <string.h>

ssize_t null_read(file_descriptor_t * file, void * buf, size_t count)        {return 0;}
ssize_t null_write(file_descriptor_t * file, const void * buf, size_t count) {return count;}
static struct dev_operations null_ops = {
    .read  = null_read,
    .write = null_write,
};
ssize_t zero_read(file_descriptor_t * file, void * buf, size_t count) {
    memset(buf, 0, count);
    return count;
}
ssize_t zero_write(file_descriptor_t * file, const void * buf, size_t count) {return count;}
static struct dev_operations zero_ops = {
    .read  = zero_read,
    .write = zero_write,
};

ssize_t random_read(file_descriptor_t * file, void * buf, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ((unsigned char*)buf)[i] = rand() % 0xFF;
    }
    return count;
}
ssize_t random_write(file_descriptor_t * file, const void * buf, size_t count) {return count;}
static struct dev_operations random_ops = {
    .read  = random_read,
    .write = random_write,
};


void dev_register_basic_devices() {
    dev_register_ops(GET_DEV(DEV_MAJ_MISC, DEV_MISC_NULL),   &null_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_MISC, DEV_MISC_ZERO),   &zero_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_MISC, DEV_MISC_RANDOM), &random_ops);
}