#include "fs/vfs.h"

#define X(a) extern const struct vfs_ops a ## _op;
FSTYPES
#undef X
#define X(a) [FS_ ## a] = &a ## _op,
const struct vfs_ops * fs_operations[SUPPORTED_FS_COUNT] = {
    FSTYPES
};
#undef X
#define X(a) [FS_ ## a] = #a,
const char * fs_names[SUPPORTED_FS_COUNT] = {
    FSTYPES
};
#undef X