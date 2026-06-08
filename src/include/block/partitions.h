#ifndef BLOCK_PARTITIONS_H
#define BLOCK_PARTITIONS_H
#include <UnstableOS/devs.h>
#include "fs/fs.h"

// size of 0 means unused
struct partition {
    off_t start; // in bytes to make medium agnostic
    off_t size; // in bytes to make seek easier
};
off_t part_seek(file_descriptor_t *file, off_t off, int whence);
ssize_t part_pread(file_descriptor_t *file, void *buf, size_t count, off_t offset);
ssize_t part_pwrite(file_descriptor_t *file, const void *buf, size_t count, off_t offset);

long part_del(dev_t old_part);
long part_add(dev_t new_part, struct partition part);

long mbr_parse_table(dev_t drive);

#endif