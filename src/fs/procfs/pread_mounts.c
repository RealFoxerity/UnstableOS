#include "mm/kernel_memory.h"
#include "mm/pmm.h"
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include "rbtree.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "procfs.h"
#include "lowlevel.h"
#include "dev_ops.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>

// source (device/"file"/smth), mountpoint, fs, options
// TODO: increase when file name resolution
#define MAX_SOURCE 64
#define MAX_BUF (MAX_SOURCE + PATH_MAX + 128)

ssize_t procfs_pread_mounts(file_descriptor_t * fd, void * buf, size_t n, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    if (!S_ISREG(fd->inode->mode) && !S_ISLNK(fd->inode->mode))
        return -EINVAL;
    if (INO_GET_FOLDER(fd->inode->id))
        return -EINVAL;
    if (!buf)
        return -EFAULT;
    if (n == 0)
        return 0;
    if (n > INT_MAX)
        n = INT_MAX;
    if (offset < 0 || offset > INT_MAX)
        return 0;

    ssize_t read_bytes = 0;

    char dev_buf[MAX_SOURCE];
    char * mount_buf = kalloc(MAX_BUF);
    char * path_buf = kalloc(PATH_MAX);
    const char * fstype = NULL;
    if (!mount_buf || !path_buf)
        return kfree(mount_buf), kfree(path_buf), -ENOMEM;

    rw_spinlock_acquire_read(&mount_lock);

    superblock_t * sb = mounts;
    while (sb && read_bytes < n) {
        if (!sb->mountpoint) goto next;
        int res = get_dir_name(sb->mountpoint, path_buf);
        if (res < 0)
            strcpy(path_buf, "<unavail>");
        if (sb->fd && sb->fd->inode) {
            if (S_ISBLK(sb->fd->inode->mode) || S_ISCHR(sb->fd->inode->mode)) {
                if (sb->fd->inode->device == (dev_t)-1) {
                    strcpy(dev_buf, "none");
                } else {
                    strcpy(dev_buf, "/dev/");
                    dev2string(sb->fd->inode->device, dev_buf+5);
                }
            }
            else
                strcpy(dev_buf, "<file>");
        } else
            strcpy(dev_buf, "none");
        if (sb->fs_type < SUPPORTED_FS_COUNT)
            fstype = fs_names[sb->fs_type];
        else
            fstype = "<unk>";

        int len = snprintf(mount_buf, MAX_BUF,
            "%s %s %s %s\n",
            dev_buf,
            path_buf,
            fstype,
            sb->mount_options & MOUNT_RDONLY ? "ro" : "rw");
        if (len <= 0)
            goto next;
        if (len <= offset) {
            offset -= len;
            goto next;
        }
        if (read_bytes + (len - offset) > n)
            len = (int)(n - read_bytes + offset);
        memcpy(buf, mount_buf + offset, len - offset);
        read_bytes += (ssize_t)(len - offset);
        buf += len - offset;
        offset = 0;
        next:
        sb = sb->next;
    }

    rw_spinlock_release_read(&mount_lock);
    kfree(mount_buf);
    kfree(path_buf);
    return read_bytes;
}