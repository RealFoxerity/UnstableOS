#include "mm/kernel_memory.h"
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include "rbtree.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "procfs.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>

#define X(m, v) [v] = #m,
static const char * main_file_names[] = {
    XFILES
};
#undef X

#define X(m, v) [v] = #m,
static const char * proc_file_names[] = {
    XPROCFILES
};
#undef X

#define PROCFS_FOLDER_MOUNTS 1
#define PROCFS_PROC_FOLDER_FD 1
#define PROCFS_PROC_FOLDER_FDINFO 2

#define INO_GET_PID(ino) ((ino) >> 32)
#define INO_GET_FOLDER(ino) (((ino) >> 16) & 0xFF)
#define INO_GET_FILE(ino) ((ino) & 0xFFFF)
#define GET_INO(pid, folder, file) (((off_t)(pid)<<32) | ((folder) << 16) | (file))

static int procfs_init(superblock_t * sb) {
    sb->mount_options |= MOUNT_RDONLY;
    return 0;
}
static int procfs_deinit(superblock_t * sb) {
    return 0;
}

static int procfs_lookup(superblock_t * sb, inode_t * last, const char * pathname, inode_t ** inode_out, unsigned short flags) {
    if (!pathname || !inode_out)
        return -EFAULT;
    if (last && !S_ISDIR(last->mode))
        return -ENOTDIR;
    ino_t ino = last ? last->id : 0;
    if (ino == 0 && strcmp(pathname, "..") == 0)
        return VFS_LOOKUP_ESCAPE;
    inode_t new = {0};
    inode_t * tgt = NULL; // for hardlinks
    new.backing_superblock = sb;
    if (strcmp(pathname, ".") == 0) {
        if (last) {
            memcpy(&new, last, sizeof(inode_t));
            return register_inode(&new, inode_out, flags);
        }
        return_root:
        new.mode = 0555 | S_IFDIR;
        return register_inode(&new, inode_out, flags);
    }
    if (!INO_GET_PID(ino)) {
        if (strcmp(pathname, "..") == 0) {
            // only possible in the mounts subfolder
            goto return_root;
        }
        if (!isalnum(pathname[0]))
            return -ENOENT;
        if (INO_GET_FOLDER(ino)) {
            kprintf("TODO: procfs mounts lookup\n");
            return -ENOENT;
        }

        if (isdigit(pathname[0])) {
            char * end;
            pid_t pid = strtol(pathname, &end, 10);
            if (!end || *end || pid <= 0)
                return -ENOENT;

            rw_spinlock_acquire_read(&process_tree_lock);
            process_t * proc = sched_find_process(pid);
            if (proc) {
                new.id = GET_INO(proc->pid, 0, 0);
                new.mode = 0555 | S_IFDIR;
                new.uid = proc->uid;
                new.gid = proc->gid;
                new.atime = new.btime = new.ctime = new.mtime = proc->startup_realtime;
            }
            rw_spinlock_release_read(&process_tree_lock);
            if (!proc)
                return -ENOENT;
            return register_inode(&new, inode_out, flags);
        }
        if (strcmp(pathname, "mounts") == 0) {
            new.id = GET_INO(0, PROCFS_FOLDER_MOUNTS, 0);
            new.mode = 0555 | S_IFDIR;
            return register_inode(&new, inode_out, flags);
        }
        for (int i = 0; i < sizeof(main_file_names) / sizeof(main_file_names[0]); i++) {
            if (!main_file_names[i])
                continue;
            if (strcmp(main_file_names[i], pathname) == 0) {
                new.id = GET_INO(0, 0, i);
                new.mode = 0444 | (i == PROCFS_self ? S_IFLNK | 0111 : S_IFREG);
                return register_inode(&new, inode_out, flags);
            }
        }
        return -ENOENT;
    }

    kassert(last);

    rw_spinlock_acquire_read(&process_tree_lock);
    process_t * proc = sched_find_process(INO_GET_PID(ino));
    if (!proc) {
        esrch:
        rw_spinlock_release_read(&process_tree_lock);
        return -ENOENT;
    }
    if (!INO_GET_FOLDER(ino) && strcmp(pathname, "cwd") == 0) {
        if (proc->do_cleanup)
            goto esrch;
        spinlock_acquire(&proc->lock);
        if (proc->do_cleanup || !proc->pwd) {
            spinlock_release(&proc->lock);
            goto esrch;
        }
        tgt = proc->pwd;

        xlink:
        __atomic_add_fetch(&tgt->instances, 1, __ATOMIC_ACQUIRE);
        spinlock_release(&proc->lock);
        rw_spinlock_release_read(&process_tree_lock);
        *inode_out = tgt;
        return 0;
    }
    if (!INO_GET_FOLDER(ino) && strcmp(pathname, "root") == 0) {
        if (proc->do_cleanup)
            goto esrch;
        spinlock_acquire(&proc->lock);
        if (proc->do_cleanup || !proc->root) {
            spinlock_release(&proc->lock);
            goto esrch;
        }
        tgt = proc->root;
        goto xlink;
    }
    if (!INO_GET_FOLDER(ino) && strcmp(pathname, "exe") == 0) {
        if (proc->do_cleanup)
            goto esrch;
        spinlock_acquire(&proc->lock);
        if (proc->do_cleanup || !proc->main_elf) {
            spinlock_release(&proc->lock);
            goto esrch;
        }
        tgt = proc->main_elf;
        goto xlink;
    }
    rw_spinlock_release_read(&process_tree_lock);

    if (strcmp(pathname, "..") == 0) {
        if (INO_GET_FOLDER(ino)) {
            // fd or fdinfo
            new.id = GET_INO(INO_GET_PID(ino), 0, 0);
            proc_folder:
            new.mode = 0555 | S_IFDIR;
            new.uid = last->uid;
            new.gid = last->gid;
            return register_inode(&new, inode_out, flags);
        }
        goto return_root;
    }
    if (INO_GET_FOLDER(ino)) {
        // fd or fdinfo
        if (!isdigit(pathname[0]))
            return -ENOENT;

        char * end;
        int fd = strtol(pathname, &end, 10);
        if (!end || *end || fd < 0 || fd >= OPEN_MAX)
            return -ENOENT;

        rw_spinlock_acquire_read(&process_tree_lock);
        proc = sched_find_process(INO_GET_PID(ino));
        if (proc && proc->do_cleanup) { // all file pointers are invalidated by cleanup
            rw_spinlock_release_read(&process_tree_lock);
            return -ENOENT;
        }
        if (proc && proc->fds[fd]) {
            if (INO_GET_FOLDER(ino) == PROCFS_PROC_FOLDER_FDINFO) {
                new.id = GET_INO(INO_GET_PID(ino), PROCFS_PROC_FOLDER_FDINFO, fd + 1);
                new.mode = 0444 | S_IFREG;
                new.uid = last->uid;
                new.gid = last->gid;
            } else {
                spinlock_acquire(&proc->lock);
                if (!proc->fds[fd] || proc->do_cleanup) {
                    spinlock_release(&proc->lock);
                    rw_spinlock_release_read(&process_tree_lock);
                    return -ENOENT;
                }

                tgt = proc->fds[fd]->inode;
                __atomic_add_fetch(&tgt->instances, 1, __ATOMIC_ACQUIRE);

                spinlock_release(&proc->lock);
            }
        } else
            proc = NULL;
        rw_spinlock_release_read(&process_tree_lock);
        if (!proc)
            return -ENOENT;
        if (INO_GET_FOLDER(ino) == PROCFS_PROC_FOLDER_FD) {
            if (!tgt)
                return -ENOENT;
            *inode_out = tgt;
            return 0;
        }
        return register_inode(&new, inode_out, flags);
    }
    if (!isalpha(pathname[0]))
        return -ENOENT;
    if (strcmp(pathname, "fd") == 0) {
        new.id = GET_INO(INO_GET_PID(ino), PROCFS_PROC_FOLDER_FD, 0);
        goto proc_folder;
    }
    if (strcmp(pathname, "fdinfo") == 0) {
        new.id = GET_INO(INO_GET_PID(ino), PROCFS_PROC_FOLDER_FDINFO, 0);
        goto proc_folder;
    }

    for (int i = 0; i < sizeof(proc_file_names) / sizeof(proc_file_names[0]); i++) {
        if (!proc_file_names[i])
            continue;
        if (strcmp(proc_file_names[i], pathname) == 0) {
            new.id = GET_INO(INO_GET_PID(ino), 0, i);
            new.mode = 0444 | S_IFREG;
            new.uid = last->uid;
            new.gid = last->gid;
            return register_inode(&new, inode_out, flags);
        }
    }
    return -ENOENT;
}

static ssize_t procfs_readdir(file_descriptor_t * fd, struct dirent * dent, size_t dent_size, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    if (!dent)
        return -EFAULT;
    if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
    if (offset < 0)
        return -ENOENT;

    if (dent_size < sizeof(struct dirent) + 2)
        return -EINVAL;

    pid_t pid = INO_GET_PID(fd->inode->id);
    int folder = INO_GET_FOLDER(fd->inode->id);
    if (pid) {
        rw_spinlock_acquire_read(&process_tree_lock);
        process_t * proc = sched_find_process(pid);
        rw_spinlock_release_read(&process_tree_lock);
        if (!proc)
            return -ENOENT;
    }
    if (offset == 0) {
        strcpy(dent->d_name, ".");
        dent->d_reclen = sizeof(struct dirent) + 2;
        dent->d_ino = fd->inode->id;
        dent->d_off = offset;
        dent->d_type = DT_DIR;
        goto end;
    }

    if (offset == 1) {
        if (dent_size < sizeof(struct dirent) + 3)
            return -EINVAL;
        strcpy(dent->d_name, "..");
        dent->d_reclen = sizeof(struct dirent) + 3;
        dent->d_ino = GET_INO(folder ? pid : 0, 0, 0);
        dent->d_off = offset;
        dent->d_type = DT_DIR;
        goto end;
    }
    if (pid == 0 && folder == PROCFS_FOLDER_MOUNTS) {
        kprintf("TODO: procfs mounts readdir\n");
        return 0;
    }
    if (pid == 0 && folder)
        return 0;
    if (pid == 0) {
        if (offset < sizeof(main_file_names) / sizeof(main_file_names[0]) + 1) {
            if (dent_size < sizeof(struct dirent) + sizeof(main_file_names[offset-1]))
                return -EINVAL;
            strcpy(dent->d_name, main_file_names[offset-1]);
            dent->d_reclen = sizeof(struct dirent) + sizeof(main_file_names[offset-1]);
            dent->d_ino = GET_INO(0, 0, offset - 1);
            dent->d_off = offset;
            dent->d_type = offset - 2 == PROCFS_self ? DT_LNK : DT_REG;
            goto end;
        }
        // pid_t is a signed long
        if (offset - sizeof(main_file_names) / sizeof(main_file_names[0]) - 2 > LONG_MAX)
            return 0;
        // +1 because we don't want to list the kernel task
        pid_t target_pid = (pid_t)(offset - sizeof(main_file_names) / sizeof(main_file_names[0]) - 1) + 1;

        rw_spinlock_acquire_read(&process_tree_lock);
        process_t * proc = (process_t*)rbtree_search_gte(process_tree, target_pid);
        if (!proc) {
            rw_spinlock_release_read(&process_tree_lock);
            return 0;
        }
        target_pid = proc->pid;
        rw_spinlock_release_read(&process_tree_lock);

        offset = target_pid + sizeof(main_file_names) / sizeof(main_file_names[0]) + 1 - 1;
        char temp[16]; // a little more than max long format
        itoad(target_pid, temp);
        if (dent_size < sizeof(struct dirent) + strlen(temp) + 1)
            return -EFAULT;
        strcpy(dent->d_name, temp);
        dent->d_reclen = sizeof(struct dirent) + strlen(temp) + 1;
        dent->d_ino = GET_INO(target_pid, 0, 0);
        dent->d_off = offset;
        dent->d_type = DT_DIR;
        goto end;
    }

    if (folder) {
        if (offset >= OPEN_MAX + 2)
            return 0;

        // TODO: when increasing OPEN_MAX, change this
        // +4 because OPEN_MAX is 128, so 3 chars + null
        if (dent_size < sizeof(struct dirent) + 4)
            return -EINVAL;
        // fd or fdinfo
        rw_spinlock_acquire_read(&process_tree_lock);
        process_t * proc = sched_find_process(pid);
        if (!proc) {
            rw_spinlock_release_read(&process_tree_lock);
            return -ENOENT;
        }
        spinlock_acquire(&proc->lock);
        dent->d_reclen = 0;
        for (; offset - 2 < OPEN_MAX; offset++) {
            if (proc->fds[offset - 2]) {
                dent->d_reclen = sizeof(struct dirent) +
                    snprintf(dent->d_name, dent_size - sizeof(struct dirent), "%lld", offset - 2);
                dent->d_type = IFTODT(proc->fds[offset-2]->inode->mode & S_IFMT);
                dent->d_ino = GET_INO(pid, folder, offset - 1);
                dent->d_off = offset;
                break;
            }
        }
        if (offset == OPEN_MAX + 2)
            offset--;

        spinlock_release(&proc->lock);
        rw_spinlock_release_read(&process_tree_lock);
        goto end;
    }

    if (offset >= sizeof(proc_file_names) / sizeof(proc_file_names[0]) + 3)
        return 0;

    if (offset == sizeof(proc_file_names) / sizeof(proc_file_names[0]) + 1) {
        if (dent_size < sizeof(struct dirent) + sizeof("fd"))
            return -EINVAL;
        strcpy(dent->d_name, "fd");
        dent->d_reclen = sizeof(struct dirent) + sizeof("fd");
        dent->d_ino = GET_INO(pid, PROCFS_PROC_FOLDER_FD, 0);
        dent->d_off = offset;
        dent->d_type = DT_DIR;
        goto end;
    }
    if (offset == sizeof(proc_file_names) / sizeof(proc_file_names[0]) + 2) {
        if (dent_size < sizeof(struct dirent) + sizeof("fdinfo"))
            return -EINVAL;
        strcpy(dent->d_name, "fdinfo");
        dent->d_reclen = sizeof(struct dirent) + sizeof("fdinfo");
        dent->d_ino = GET_INO(pid, PROCFS_PROC_FOLDER_FDINFO, 0);
        dent->d_off = offset;
        dent->d_type = DT_DIR;
        goto end;
    }

    if (dent_size < sizeof(struct dirent) + sizeof(proc_file_names[offset-1]))
        return -EINVAL;
    strcpy(dent->d_name, proc_file_names[offset-1]);
    dent->d_reclen = sizeof(struct dirent) + sizeof(proc_file_names[offset-1]);
    dent->d_ino = GET_INO(pid, 0, offset - 1);
    dent->d_off = offset;
    dent->d_type = DT_REG;

    end:
    rw_spinlock_acquire_write(&fd->access_lock);
    fd->off = offset + 1;
    rw_spinlock_release_write(&fd->access_lock);
    return dent->d_reclen;
}

static ssize_t procfs_pread(file_descriptor_t * fd, void * buf, size_t n, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    if (!INO_GET_PID(fd->inode->id))
        return procfs_pread_main(fd, buf, n, offset);
    return procfs_pread_proc(fd, buf, n, offset);
}

const struct vfs_ops procfs_op = {
    .fs_init = procfs_init,
    .fs_deinit = procfs_deinit,

    .lookup = procfs_lookup,

    .pread = procfs_pread,

    .readdir = procfs_readdir,
};