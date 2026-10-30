#include "fs/fs.h"
#include "fs/vfs.h"
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

int get_dir_name(inode_t * dir, char out[PATH_MAX]) {
    if (!dir)
        return -EINVAL;
    if (!S_ISDIR(dir->mode) || !dir->backing_superblock)
        return -ENOTDIR;

    struct dirent * dent = kalloc(sizeof(struct dirent) + FILENAME_MAX);
    if (dent == NULL)
        return -ENOMEM;

    out[PATH_MAX - 1] = '\0';

    __atomic_add_fetch(&dir->instances, 1, __ATOMIC_RELEASE);

    unsigned long curr_dir_fsid = -1;
    ino_t curr_dir_ino = -1;

    size_t rem = PATH_MAX - 1;

    inode_t * current_dir = dir;
    inode_t * parent_dir = NULL;

    int noxdev = current_dir->next_superblock ? O_NOXDEV : 0;

    while (1) {
        curr_dir_ino = current_dir->id;
        kassert(current_dir->backing_superblock);
        curr_dir_fsid = current_dir->backing_superblock->fsid;

        int status = openat_inode(current_dir, "..", O_RDONLY | O_DIRECTORY, 0, &parent_dir, 1);
        close_inode(current_dir);
        if (status < 0 || parent_dir == NULL) {
            kfree(dent);
            return status;
        }
        if (parent_dir->backing_superblock == NULL ||
            parent_dir->backing_superblock->funcs == NULL ||
            parent_dir->backing_superblock->funcs->readdir == NULL) {
            close_inode(parent_dir);
            kfree(dent);
            return -ENOENT;
        }

        file_descriptor_t faked = {
            .flags = O_RDONLY,
            .instances = 1,
            .inode = parent_dir,
        };
        status = parent_dir->backing_superblock->funcs->readdir(&faked, dent, sizeof(struct dirent) + FILENAME_MAX, 0);
        if (status == 0)
            status = -ENOENT;
        if (status < 0) {
            close_inode(parent_dir);
            kfree(dent);
            return status;
        }
        while (1) {
            status = parent_dir->backing_superblock->funcs->readdir(&faked, dent, sizeof(struct dirent) + FILENAME_MAX, faked.off);
            if (status == 0)
                status = -ENOENT;
            if (status < 0) {
                close_inode(parent_dir);
                kfree(dent);
                return status;
            }

            inode_t * temp;
            status = openat_inode(parent_dir, dent->d_name, O_SEARCH | O_DIRECTORY | noxdev, 0, &temp, 1);
            if (status < 0 || !temp)
                continue; // maybe, hopefully, we didn't fail on the actual entry
            if (!temp->backing_superblock) {
                close_inode(temp);
                continue;
            }

            if (temp->id == curr_dir_ino && temp->backing_superblock->fsid == curr_dir_fsid) {
                close_inode(temp);
                break;
            }
            close_inode(temp);
        }
        noxdev = 0;

        current_dir = parent_dir;

        size_t component_len = strlen(dent->d_name);
        if (strcmp(dent->d_name, "..") == 0) break;
        if (rem < component_len) {
            close_inode(current_dir);
            return -ENAMETOOLONG;
        }

        memcpy(out + rem - component_len, dent->d_name, component_len);
        rem -= component_len;
        out[--rem] = '/';
    }
    close_inode(current_dir);

    if (rem == PATH_MAX - 1) {
        // we are at root, so most of the logic is skipped
        out[0] = '/';
        out[1] = '\0';
    } else {
        // move stuff back to the buffer beginning
        memmove(out, out + rem, PATH_MAX - rem);
    }
    return 0;
}