#include "mm/kernel_memory.h"
#include "mm/pmm.h"
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include "rbtree.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "procfs.h"
#include "lowlevel.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#define PROCFS_BUFSIZ 1024

ssize_t procfs_pread_proc(file_descriptor_t * fd, void * buf, size_t n, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    if (!S_ISREG(fd->inode->mode))
        return -EINVAL;
    if (!INO_GET_PID(fd->inode->id))
        return -EINVAL;
    if (INO_GET_FOLDER(fd->inode->id) && INO_GET_FOLDER(fd->inode->id) != PROCFS_PROC_FOLDER_FDINFO) // how?
        return -EINVAL;
    if (!buf)
        return -EFAULT;
    if (n == 0)
        return 0;

    pid_t pid = INO_GET_PID(fd->inode->id);
    int file = INO_GET_FILE(fd->inode->id) - 1;
    if (file < 0 || file >= OPEN_MAX)
        return -EBADF;

    size_t len = 0;
    char temp[PROCFS_BUFSIZ];
    if (INO_GET_FOLDER(fd->inode->id)) {
        // fdinfo

        if (offset >= PROCFS_BUFSIZ)
            return 0;

        rw_spinlock_acquire_read(&process_tree_lock);
        process_t * proc = sched_find_process(pid);
        if (!proc || !proc->fds[file]) {
            rw_spinlock_release_read(&process_tree_lock);
            return -EBADF;
        }
        spinlock_acquire(&proc->lock);
        if (!proc->fds[file]) {
            spinlock_release(&proc->lock);
            rw_spinlock_release_read(&process_tree_lock);
            return -EBADF;
        }
        int flags = proc->fd_flags[file];
        file_descriptor_t * targetfd = proc->fds[file];
        __atomic_add_fetch(&targetfd->instances, 1, __ATOMIC_ACQUIRE);
        spinlock_release(&proc->lock);
        rw_spinlock_release_read(&process_tree_lock);

        len = snprintf(temp, PROCFS_BUFSIZ,
"pos:    %lld\n"
"flags:  %#o\n"
"mnt_id: %lu\n"
"ino:    %lld\n",
            targetfd->off,
            targetfd->flags | flags,
            targetfd->inode->backing_superblock ? targetfd->inode->backing_superblock->fsid : 0,
            targetfd->inode->id);
        close_file(targetfd);

        from_temp:
        if (offset >= len)
            return 0;
        if (offset + n > len)
            n = len - offset;
        memcpy(buf, temp + offset, n);
        return (ssize_t)n;
    }

    rw_spinlock_acquire_read(&process_tree_lock);
    process_t * proc = sched_find_process(pid);
    if (!proc) {
        rw_spinlock_release_read(&process_tree_lock);
        return -EBADF;
    }
    file++;
    switch (file) {
        case PROCFS_PROC_cmdline:
        case PROCFS_PROC_comm: {
            ssize_t ret = procfs_pread_cmdline(proc, buf, n, offset, file == PROCFS_PROC_comm);
            rw_spinlock_release_read(&process_tree_lock);
            return ret;
        }
        case PROCFS_PROC_status:
            len = snprintf(temp, PROCFS_BUFSIZ, ""
"Umask:\t%o\n"
"State:\t%s\n"
"Pid:\t%ld\n"
"Ppid:\t%ld\n"
"Pgid:\t%ld\n"
"Sid:\t%ld\n"
"Uid:\t%lu\t%lu\t%lu\n"
"Gid:\t%lu\t%lu\t%lu\n"
"Groups:",
                __atomic_load_n(&proc->umask, __ATOMIC_RELAXED),
                proc->is_stopped ? "T (stopped)" :
                    proc->do_cleanup ? "Z (zombie)" :
                    "R (running)",
                proc->pid,
                proc->parent->pid,
                proc->pgrp,
                proc->session,
                __atomic_load_n(&proc->uid, __ATOMIC_RELAXED),
                __atomic_load_n(&proc->euid, __ATOMIC_RELAXED),
                __atomic_load_n(&proc->suid, __ATOMIC_RELAXED),
                __atomic_load_n(&proc->gid, __ATOMIC_RELAXED),
                __atomic_load_n(&proc->egid, __ATOMIC_RELAXED),
                __atomic_load_n(&proc->sgid, __ATOMIC_RELAXED)
            );

            gid_t groups[NGROUPS_MAX];
            unsigned int ngroup;

            spinlock_acquire(&proc->lock);
            memcpy(groups, proc->sup_groups, sizeof(gid_t) * proc->ngroup);
            ngroup = proc->ngroup;
            spinlock_release(&proc->lock);

            for (int i = 0; i < ngroup; i++) {
                len += snprintf(temp + len, PROCFS_BUFSIZ - len, "\t%lu", groups[i]);
            }
            len += snprintf(temp + len, PROCFS_BUFSIZ - len, "\nSigPnd:\t%016llx\n", proc->sa_pending);

            rw_spinlock_release_read(&process_tree_lock);
            goto from_temp;
        case PROCFS_PROC_ctty:
            len = snprintf(temp, PROCFS_BUFSIZ, "%d:%d", MAJOR(proc->ctty), MINOR(proc->ctty));
            rw_spinlock_release_read(&process_tree_lock);
            goto from_temp;
        case PROCFS_PROC_times:
            len = snprintf(temp, PROCFS_BUFSIZ,
"StartReal:\t%lld\n"
"NextAlrm:\t%lld\n"
"UClicks:\t%lld\n"
"SClicks:\t%lld\n"
"DUClicks:\t%lld\n"
"DSClicks:\t%lld\n",
                proc->startup_realtime,
                proc->next_alarm,
                proc->user_clicks,
                proc->system_clicks,
                proc->dead_user_clicks,
                proc->dead_system_clicks);

            rw_spinlock_release_read(&process_tree_lock);
            goto from_temp;
        default:
            rw_spinlock_release_read(&process_tree_lock);
            return -EBADF;
    }
}