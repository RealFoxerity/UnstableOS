#include "kernel.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#include <errno.h>


pid_t sys_getpgid(pid_t pid) {
    if (pid == 0) return current_process->pgrp;

    rw_spinlock_acquire_read(&process_tree_lock);
    process_t * proc = sched_find_process(pid);
    if (!proc) {
        rw_spinlock_release_read(&process_tree_lock);
        return -ESRCH;
    }
    pid_t pgid = proc->pgrp;
    rw_spinlock_release_read(&process_tree_lock);
    return pgid;
}

pid_t sys_getsid(pid_t pid) {
    if (pid == 0) return current_process->session;

    rw_spinlock_acquire_read(&process_tree_lock);
    process_t * proc = sched_find_process(pid);
    if (!proc) {
        rw_spinlock_release_read(&process_tree_lock);
        return -ESRCH;
    }
    pid_t sid = proc->session;
    rw_spinlock_release_read(&process_tree_lock);
    return sid;
}

pid_t sys_setsid() {
    spinlock_acquire_interruptible(&current_process->lock);
    if (current_process->pgrp_members || current_process->pid == current_process->pgrp) {
        spinlock_release(&current_process->lock);
        return -EPERM;
    }

    spinlock_acquire(&scheduler_lock);
    if (current_process->pgrp_leader) {
        __atomic_sub_fetch(&current_process->pgrp_leader->pgrp_members, 1, __ATOMIC_RELAXED);
    }

    current_process->pgrp = current_process->session = current_process->pid;
    current_process->pgrp_leader = current_process;
    current_process->pgrp_members = 0;

    // for waiting on process groups, we need to inform the parent stuff changed
    for (thread_t * thread = current_process->parent->threads; thread != NULL; thread = thread->next) {
        if (thread->status == SCHED_WAITING)
            thread->status =  SCHED_RUNNABLE;
    }
    spinlock_release(&scheduler_lock);

    current_process->ctty = 0;

    spinlock_release(&current_process->lock);

    return current_process->pid;
}

int sys_setpgid(pid_t pid, pid_t pgid) {
    if (pid < 0 || pgid < 0) {
        return -EINVAL;
    }

    rw_spinlock_acquire_read(&process_tree_lock);

    process_t * target_process = NULL;
    process_t * target_pgrp = NULL;
    if (pid == 0 || pid == current_process->pid)
        target_process = current_process;
    else
        target_process = sched_find_process(pid);
    if (pgid == 0 || pgid == current_process->pid) {
        target_pgrp = current_process;
    } else {
        if (pid == pgid)
            target_pgrp = target_process;
        else
            target_pgrp = sched_find_process(pgid);
    }

    if (target_process == NULL || target_pgrp == NULL) {
        rw_spinlock_release_read(&process_tree_lock);
        return -ESRCH;
    }

    spinlock_acquire_interruptible(&current_process->lock);
    if (target_process != current_process)
        spinlock_acquire_interruptible(&target_process->lock);
    if (target_process != target_pgrp && target_pgrp != current_process)
        spinlock_acquire_interruptible(&target_pgrp->lock);
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
        if (target_process != target_pgrp && target_pgrp != current_process)
            spinlock_release(&target_pgrp->lock);

        rw_spinlock_release_read(&process_tree_lock);
        return -EPERM;
    }

    if (pid != 0 && pid != current_process->pid) {
        char child_after_exec = 0;
        char not_a_child = 0;

        process_t * tmp = target_process;
        while (tmp->pid != 0 && tmp != current_process) {
            if (tmp->after_exec) {
                child_after_exec = 1;
                //break; // we prefer returning -ESRCH and not -EACCESS (more POSIX-y)
            }
            tmp = tmp->parent;
        }
        if (tmp->pid == 0)
            not_a_child = 1;

        if (child_after_exec || not_a_child) {
            spinlock_release(&current_process->lock);
            if (target_process != current_process)
                spinlock_release(&target_process->lock);
            if (target_process != target_pgrp && target_pgrp != current_process)
                spinlock_release(&target_pgrp->lock);

            rw_spinlock_release_read(&process_tree_lock);
            if (not_a_child)
                return -ESRCH;
            return -EACCES;
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
    if (target_process != target_pgrp && target_pgrp != current_process)
        spinlock_release(&target_pgrp->lock);

    rw_spinlock_release_read(&process_tree_lock);

    spinlock_acquire(&scheduler_lock);
    for (thread_t * thread = current_process->parent->threads; thread != NULL; thread = thread->next) {
        if (thread->status == SCHED_WAITING)
            thread->status =  SCHED_RUNNABLE;
    }
    spinlock_release(&scheduler_lock);
    return 0;
}