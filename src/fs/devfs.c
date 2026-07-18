#include "fs/devfs.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "../../libc/src/include/UnstableOS/devs.h"
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

#define HD_ENTRY(no) \
    {\
        .name = "hd"#no,\
        {\
            .st_rdev = GET_DEV(DEV_MAJ_BLOCK0, DEV_BLOCK_DRIVE##no),\
            .st_mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,\
        }\
    },

#define HD_PART(no, pno) \
    {\
        .name = "hd"#no"p"#pno,\
        {\
            .st_rdev = GET_DEV(DEV_MAJ_BLOCK0, DEV_BLOCK_DRIVE##no + pno),\
            .st_mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,\
        }\
    },
#define HD(no)\
    HD_ENTRY(no)\
    HD_PART(no, 1)\
    HD_PART(no, 2)\
    HD_PART(no, 3)\
    HD_PART(no, 4)

#define HDS_BLOCK0 \
    HD(0)\
    HD(1)\
    HD(2)\
    HD(3)


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
    HDS_BLOCK0
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
    {
        .name = "random",
        {
            .st_rdev = GET_DEV(DEV_MAJ_MISC, DEV_MISC_RANDOM),
            .st_mode = S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
        }
    },
    {
        .name = "fb0",
        {
            .st_rdev = GET_DEV(DEV_MAJ_FB, 0),
            .st_mode = S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
        }
    },

};

// no need for others since we give out raw devices and they bypass the superblock vfs system
const struct vfs_ops devfs_op = {
    .lookup = devfs_lookup,
    .seek = devfs_seek,
    .readdir = devfs_readdir
};

// there are no subfolders, so we just iterate the list and ignore the last parameter
int devfs_lookup(superblock_t * sb, inode_t * last, const char * pathname, inode_t ** inode_out) {
    kassert(sb);
    kassert(pathname);

    size_t pathlen = strlen(pathname);
    if (strcmp(pathname, "..") == 0) return VFS_LOOKUP_ESCAPE;

    size_t devfs_id = 0;
    if (!(pathlen == 0 || (pathlen == 1 && pathname[0] == '.'))) {
        for (int i = 0; i < sizeof(devfs_files)/sizeof(struct devfs_node); i++) {
            if (strcmp(pathname, devfs_files[i].name) == 0) {
                devfs_id = i + 2;
                break;
            }
        }
    }
    if (devfs_id == 0 && !(pathlen == 1 && pathname[0] == '.')) return -ENOENT;

    long status = register_inode(&(inode_t) {
         .id                 = devfs_id,
         .backing_superblock = sb,
         .mode               = devfs_id == 0
                                ? S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
                                : devfs_files[devfs_id - 2].node_info.st_mode,
         .device = devfs_id == 0 ? 0 : devfs_files[devfs_id - 2].node_info.st_rdev,
         .nlink  = devfs_id == 0 ? 2 : 1,
       }, inode_out);

    return status;
}

// only for readdir
off_t devfs_seek(file_descriptor_t * fd, off_t off, int whence) {
    return generic_seek(fd, off, whence, sizeof(devfs_files)/sizeof(struct devfs_node) + 2);
}
ssize_t devfs_readdir(file_descriptor_t * fd, struct dirent * dent, size_t dent_size, off_t offset) {
    kassert(dent);
    kassert(fd);

    if (offset >= sizeof(devfs_files)/sizeof(struct devfs_node) + 2) return 0;

    switch (offset) {
        case 0: // "."
            if (dent_size < sizeof(struct dirent) + 2)
                return -EINVAL;
            *dent = (struct dirent) {
                .d_off = offset,
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
                .d_off = offset,
                .d_reclen = sizeof(struct dirent) + 3,
                .d_type = DT_DIR,
            };
            memcpy(dent->d_name, "..", 3);
            break;
        default:
            if (dent_size < sizeof(struct dirent) + sizeof(devfs_files[offset - 2].name))
                return -EINVAL;
            *dent = (struct dirent) {
                .d_off = offset,
                .d_reclen = sizeof(struct dirent) + sizeof(devfs_files[offset - 2].name),
                .d_type = IFTODT(devfs_files[offset - 2].node_info.st_mode & S_IFMT)
            };
            memcpy(&dent->d_name, devfs_files[offset - 2].name, sizeof(devfs_files[offset - 2].name));
    }
    offset++;
    // once again, fd->off is unsigned long long - 64 bits
    // this atomic requires cmpxchg8b, which is not on i486
    // first was i586
    // __atomic_store_n(&fd->off, offset, __ATOMIC_RELAXED);
    rw_spinlock_acquire_write(&fd->access_lock);
    fd->off = offset;
    rw_spinlock_release_write(&fd->access_lock);
    return dent->d_reclen;
}