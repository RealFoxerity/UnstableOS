#include "../include/fs/vfs.h"
#include "../include/fs/tarfs.h"
#include "../include/fs/devfs.h"

const struct vfs_ops * fs_operations[SUPPORTED_FS_COUNT] = {
    [FS_TARFS] = &tar_op,
    [FS_DEVFS] = &devfs_op,
};