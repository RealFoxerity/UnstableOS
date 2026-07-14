#include "fs/vfs.h"
#include "fs/tarfs.h"
#include "fs/devfs.h"
#include "fs/fat.h"

const struct vfs_ops * fs_operations[SUPPORTED_FS_COUNT] = {
    [FS_TARFS] = &tar_op,
    [FS_DEVFS] = &devfs_op,
    [FS_FAT]   = &fat_ops
};