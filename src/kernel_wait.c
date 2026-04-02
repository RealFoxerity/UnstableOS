#include "kernel.h"
#include <errno.h>
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include <sys/wait.h>
#include "kernel_exec.h"

static process_t * find_in_list(pid_t pid, process_t * list) {
    for (process_t * child = list; child != NULL; child = child->next) {
        if (child->parent == current_process) {
            if (pid < -1 && child->pgrp == -pid) return child;
            if (pid == -1)                       return child;
            if (pid == 0 && child->pgrp == current_process->pgrp) return child;
            if (pid >  0 && child->pid  == pid)  return child;
        }
    }
    return NULL;
}



pid_t sys_waitpid(pid_t pid, int * wstatus, int options) {
    process_t * child = NULL;

    // get the most up-to-date info
    // since each reschedule moves the process to the end
    // of the queue, this forces all terminated processes
    // to be put into the zombie list
    reschedule();

    while (1) {
        spinlock_acquire(&scheduler_lock);
        child = find_in_list(pid, zombie_list);
        if  (child == NULL && find_in_list(pid, process_list) == NULL) {
            spinlock_release(&scheduler_lock);
            return -ECHILD;
        }
        if (child != NULL) goto found;

        if (options & WNOHANG) {
            spinlock_release(&scheduler_lock);
            return 0;
        }

        current_thread->status = SCHED_WAITING;
        spinlock_release(&scheduler_lock);
        reschedule();

        if (current_thread->sa_to_be_handled) {
            if (current_thread->sa_to_be_handled != SIGCHLD) return -EINTR;
            if (current_thread->sa_info_to_be_handled.si_code & SI_USER) return -EINTR;

            // killed/exited not here because there's a delay between that signal
            // and the child process struct being in the zombie list
            if (current_thread->sa_info_to_be_handled.si_code == CLD_STOPPED) {
                if (!(options & WUNTRACED)) return -EINTR;
                if (wstatus != NULL) *wstatus = 0x000400;
                return current_thread->sa_info_to_be_handled.si_pid;
            }
            if (current_thread->sa_info_to_be_handled.si_code == CLD_CONTINUED) {
                if (!(options & WCONTINUED)) return -EINTR;
                if (wstatus != NULL) *wstatus = 0x000800;
                return current_thread->sa_info_to_be_handled.si_pid;
            }
        }
    }
    found:
    if (wstatus != NULL) *wstatus = child->postmortem_wstatus;

    // unlink the child
    UNLINK_DOUBLE_LINKED_LIST(child, zombie_list)
    pid_t child_pid = child->pid;

    __atomic_add_fetch(&current_process->dead_user_clicks, child->user_clicks, __ATOMIC_RELAXED);
    __atomic_add_fetch(&current_process->dead_system_clicks, child->system_clicks, __ATOMIC_RELAXED);

    kfree(child);
    spinlock_release(&scheduler_lock);
    return child_pid;
}