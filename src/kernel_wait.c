#include "include/kernel.h"
#include "../libc/src/include/errno.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "include/mm/kernel_memory.h"

pid_t sys_wait(int * wstatus) {
    process_t * child = NULL;

    // preliminary check whether we even have children
    spinlock_acquire(&scheduler_lock);

    for (child = zombie_list; child != NULL; child = child->next) {
        if (child->parent == current_process) goto found_child;
    }

    // will never get another process in the zombie list with sigchld disabled
    if (current_process->sa_handlers[SIGCHLD - 1].sa_handler == SIG_IGN ||
        current_process->sa_handlers[SIGCHLD - 1].sa_flags & SA_NOCLDWAIT)
    {
            spinlock_release(&scheduler_lock);
            return -ECHILD;
    }
    for (child = process_list; child != NULL; child = child->next) {
        if (child->parent == current_process) break;
    }
    spinlock_release(&scheduler_lock);
    if (child == NULL) return -ECHILD; // no zombie or running child at all

    while (1) {
        spinlock_acquire(&scheduler_lock);

        for (child = zombie_list; child != NULL; child = child->next) {
            if (child->parent == current_process) break;
        }
        if (child != NULL) break;

        if (child == NULL) {
            for (child = process_list; child != NULL; child = child->next) {
                if (child->parent == current_process) break;
            }
            if (child == NULL) {
                spinlock_release(&scheduler_lock);
                return -ECHILD;
            }
        }

        current_thread->status = SCHED_WAITING;
        spinlock_release(&scheduler_lock);
        reschedule();

        if (current_thread->sa_to_be_handled) {
            if (current_thread->sa_to_be_handled != SIGCHLD) return -EINTR;
            if (current_thread->sa_info_to_be_handled.si_code & SI_USER) return -EINTR;

            // killed/exited not here because there's a delay between that signal
            // and the child process struct being in the zombie list
            #ifdef WAIT_ACTS_AS_WUNTRACED
            if (current_thread->sa_info_to_be_handled.si_code == CLD_STOPPED) {
                *wstatus = 0x000400;
                return current_thread->sa_info_to_be_handled.si_pid;
            }
            if (current_thread->sa_info_to_be_handled.si_code == CLD_CONTINUED) {
                *wstatus = 0x000800;
                return current_thread->sa_info_to_be_handled.si_pid;
            }
            #endif
        }
    }

    found_child:
    // TODO: change when adding more wstatuses
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