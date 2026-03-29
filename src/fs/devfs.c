#include "fs/devfs.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "devs.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include <errno.h>

#include <string.h>
#include <sys/stat.h>

#define DEVFS_MAX_NODE_NAME 16

struct devfs_node {
    const char name[DEVFS_MAX_NODE_NAME];
    struct stat node_info;
};

// TODO: make a hashtable for these entries
static struct devfs_node devfs_files[] = {
    {
        .name = "mem0",
        {
            .st_rdev = GET_DEV(DEV_MAJ_MEM, DEV_MEM_MEMDISK0),
            .st_mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },
    {
        .name = "mem1",
        {
            .st_rdev = GET_DEV(DEV_MAJ_MEM, DEV_MEM_MEMDISK1),
            .st_mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },
    {
        .name = "mem2",
        {
            .st_rdev = GET_DEV(DEV_MAJ_MEM, DEV_MEM_MEMDISK2),
            .st_mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },
    {
        .name = "mem3",
        {
            .st_rdev = GET_DEV(DEV_MAJ_MEM, DEV_MEM_MEMDISK3),
            .st_mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },
    {
        .name = "tty",
        {
            .st_rdev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_CURRENT),
            .st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
        }
    },
    {
        .name = "tty0",
        {
            .st_rdev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0),
            .st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },
    {
        .name = "ttyS0",
        {
            .st_rdev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_S0),
            .st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },
    {
        .name = "kconsole",
        {
            .st_rdev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE),
            .st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },
    {
        .name = "psaux",
        {
            .st_rdev = GET_DEV(DEV_MAJ_MISC, DEV_MISC_PS2MOUSE),
            .st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },
    {
        .name = "zero",
        {
            .st_rdev = GET_DEV(DEV_MAJ_MISC, DEV_MISC_ZERO),
            .st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
        }
    },
    {
        .name = "null",
        {
            .st_rdev = GET_DEV(DEV_MAJ_MISC, DEV_MISC_ZERO),
            .st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
        }
    },

};

// no need for others since we give out raw devices and they bypass the superblock vfs system
const struct vfs_ops devfs_op = {
    .lookup = devfs_lookup,
    .seek = devfs_seek,
    .stat = devfs_stat,
    .readdir = devfs_readdir
};

// there are no subfolders, so we just iterate the list and ignore the last parameter
inode_t * devfs_lookup(superblock_t * sb, inode_t * last, const char * pathname) {
    kassert(sb);
    kassert(pathname);

    size_t pathlen = strlen(pathname);
    if (pathlen == 2 && strncmp(pathname, "..", 2) == 0) return VFS_LOOKUP_ESCAPE;

    size_t devfs_id = 0;
    if (!(pathlen == 0 || (pathlen == 1 && pathname[0] == '.'))) {
        for (int i = 0; i < sizeof(devfs_files)/sizeof(struct devfs_node); i++) {
            if (pathlen == strlen(devfs_files[i].name) &&
                strcmp(pathname, devfs_files[i].name) == 0) {
                devfs_id = i + 2;
                break;
            }
        }
    }
    if (devfs_id == 0 && !(pathlen == 1 && pathname[0] == '.')) return VFS_LOOKUP_NOTDIRECTORY;

    inode_t * ret = create_inode(sb, (void*)devfs_id);
    if (devfs_id == 0)
        inode_change_mode(ret, S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    else {
        inode_change_mode(ret, devfs_files[devfs_id - 2].node_info.st_mode);
        ret->device = devfs_files[devfs_id - 2].node_info.st_rdev;
    }
    return ret;
}

// only for readdir
off_t devfs_seek(file_descriptor_t * fd, off_t off, int whence) {
    kassert(fd);
    kassert(fd->inode)
    if (!S_ISDIR(fd->inode->mode)) return -EINVAL;

    switch (whence) {
        case SEEK_SET:
            if (off < 0) return -EINVAL;
            return fd->off = off;
        case SEEK_CUR:
            if (fd->off + off > fd->off && off < 0) return -EINVAL; // underflow - negative offset
            if (fd->off + off < fd->off && off > 0) return -E2BIG; // overflow

            return fd->off = fd->off + off;
        case SEEK_END:
            if (off >= 0) {
                if (fd->off + off > fd->off && off < 0) return -EINVAL; // underflow - negative offset
                if (fd->off + off < fd->off && off > 0) return -E2BIG; // overflow
                return fd->off = sizeof(devfs_files)/sizeof(struct devfs_node) + 2 + off;
            }
            else if (off < 0 && -off <= fd->off) return fd->off = fd->off - off;
            return -EINVAL; // negative offset
        default:
            return -EINVAL;
    }
}
ssize_t devfs_readdir(file_descriptor_t * fd, struct dirent * dent, size_t dent_size) {
    kassert(dent);
    kassert(fd);

    if (fd->off >= sizeof(devfs_files)/sizeof(struct devfs_node) + 2) return 0;

    switch (fd->off) {
        case 0: // "."
            if (dent_size < sizeof(struct dirent) + 2)
                return -EINVAL;
            *dent = (struct dirent) {
                .d_off = fd->off,
                .d_reclen = sizeof(struct dirent) + 2,
                .d_type = DT_DIR,
            };
            dent->d_name[0] = '.';
            dent->d_name[1] = '\0';
            break;
        case 1:
            if (dent_size < sizeof(struct dirent) + 3)
                return -EINVAL;
            *dent = (struct dirent) {
                .d_off = fd->off,
                .d_reclen = sizeof(struct dirent) + 3,
                .d_type = DT_DIR,
            };
            memcpy(dent->d_name, "..", 3);
            break;
        default:
            if (dent_size < sizeof(struct dirent) + sizeof(devfs_files[fd->off - 2].name))
                return -EINVAL;
            *dent = (struct dirent) {
                .d_off = fd->off,
                .d_reclen = sizeof(struct dirent) + sizeof(devfs_files[fd->off - 2].name),
                .d_type = IFTODT(devfs_files[fd->off - 2].node_info.st_mode & S_IFMT)
            };
            memcpy(&dent->d_name, devfs_files[fd->off - 2].name, sizeof(devfs_files[fd->off - 2].name));
    }
    fd->off++;
    return dent->d_reclen;
}
int devfs_stat(inode_t * file, struct stat * buf) {
    kassert(file);

    if ((size_t)file->id > sizeof(devfs_files)/sizeof(struct devfs_node) + 2) return -EINVAL;

    switch ((size_t)file->id) {
        case 0: // the dev folder
            *buf = (struct stat) {
                .st_mode = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
            };
            return 0;
        case 1: // stat for "..", which doesn't make sense
            return -EINVAL;
        default:
            *buf = devfs_files[(size_t)file->id - 2].node_info;
    }
    return 0;
}