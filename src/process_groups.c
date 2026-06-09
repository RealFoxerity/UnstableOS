#include "kernel.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#include <errno.h>


pid_t sys_getpgid(pid_t pid) {
    if (pid == 0) return current_process->pgrp;

    spinlock_acquire(&scheduler_lock);
    process_t * tested = process_list;
    while (tested != NULL) {
        if (tested->pid == pid) break;
        tested = tested->next;
    }
    if (tested != NULL) {
        spinlock_release(&scheduler_lock);
        return tested->pgrp; // found the pid
    }
    spinlock_release(&scheduler_lock);
    return -ESRCH;
}

pid_t sys_setsid() {
    spinlock_acquire(&current_process->lock);
    if (current_process->pgrp_members || current_process->pid == current_process->pgrp) {
        spinlock_release(&current_process->lock);
        return -EPERM;
    }

    if (current_process->pgrp_leader) {
        __atomic_sub_fetch(&current_process->pgrp_leader->pgrp_members, 1, __ATOMIC_RELAXED);
    }
    current_process->pgrp = current_process->session = current_process->pid;
    current_process->pgrp_leader = current_process;
    current_process->pgrp_members = 0;
    spinlock_release(&current_process->lock);

    return current_process->pid;
}

int sys_setpgid(pid_t pid, pid_t pgid) {
    if (pid < 0 || pgid < 0) {
        return -EINVAL;
    }
    spinlock_acquire(&scheduler_lock);

    process_t * target_process = NULL;
    process_t * target_pgrp = NULL;
    if (pid == 0 || pid == current_process->pid) {
        target_process = current_process;
    } else {
        for (process_t * proc = process_list; proc != NULL; proc = proc->next) {
            if (proc->pid == pid) {
                target_process = proc;
                break;
            }
        }
    }
    if (pgid == 0 || pgid == current_process->pid) {
        target_pgrp = current_process;
    } else {
        if (pid == pgid) {
            target_pgrp = target_process;
        } else {
            for (process_t * proc = process_list; proc != NULL; proc = proc->next) {
                if (proc->pid == pgid) {
                    target_pgrp = proc;
                    break;
                }
            }
        }
    }

    if (target_process == NULL || target_pgrp == NULL) {
        spinlock_release(&scheduler_lock);
        return -ESRCH;
    }

    spinlock_acquire(&current_process->lock);
    if (target_process != current_process)
        spinlock_acquire(&target_process->lock);
    if (target_process != target_pgrp)
        spinlock_acquire(&target_pgrp->lock);
    // session leaders cannot change their pgids
    // target process must be in our session
    // target process group must be in our session
    if (target_process->pid == target_process->session ||
        target_process->session != current_process->session ||
        target_pgrp->session != current_process->session)
    {
        spinlock_release(&current_process->lock);
        if (target_process != current_process)
            spinlock_release(&target_process->lock);
        if (target_process != target_pgrp)
            spinlock_release(&target_pgrp->lock);

        spinlock_release(&scheduler_lock);
        return -EPERM;
    }

    if (pid != 0 && pid == current_process->pid) {
        char child_after_exec = 0;
        char not_a_child = 0;

        process_t * tmp = target_process;
        while (tmp->pid != 0 && tmp != current_process) {
            if (tmp->after_exec) {
                child_after_exec = 1;
                break;
            }
            tmp = tmp->parent;
        }
        if (tmp->pid == 0)
            not_a_child = 1;

        if (child_after_exec || not_a_child) {
            spinlock_release(&current_process->lock);
            if (target_process != current_process)
                spinlock_release(&target_process->lock);
            if (target_process != target_pgrp)
                spinlock_release(&target_pgrp->lock);

            spinlock_release(&scheduler_lock);
            if (child_after_exec)
                return -EACCES;
            return -ESRCH;
        }
    }

    if (target_process != target_process->pgrp_leader &&
        target_process->pgrp_leader) {
        __atomic_sub_fetch(&target_process->pgrp_leader->pgrp_members, 1, __ATOMIC_RELAXED);
    }
    if (target_process == target_pgrp) {
        target_process->pgrp_members = 0;
    } else {
        __atomic_add_fetch(&target_pgrp->pgrp_members, 1, __ATOMIC_RELAXED);
    }
    target_process->pgrp_leader = target_pgrp;
    target_process->pgrp = target_pgrp->pid;

    spinlock_release(&current_process->lock);
    if (target_process != current_process)
        spinlock_release(&target_process->lock);
    if (target_process != target_pgrp)
        spinlock_release(&target_pgrp->lock);

    spinlock_release(&scheduler_lock);
    return 0;
}