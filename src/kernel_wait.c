#include "include/kernel.h"
#include "include/errno.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "include/mm/kernel_memory.h"

pid_t sys_wait(int * wstatus) {
    spinlock_acquire(&scheduler_lock);

    process_t * checked = process_list;
    while (checked != NULL) {
        if (checked->ppid == current_process->pid) break;
        checked = checked->next;
    }

    if (checked == NULL) {
        spinlock_release(&scheduler_lock);
        return ECHILD;
    }

    spinlock_release(&scheduler_lock);

    process_t * child = NULL;
    while (1) {
        spinlock_acquire(&scheduler_lock);

        checked = zombie_list;
        while (checked != NULL) {
            if (checked->ppid == current_process->pid) {
                child = checked;
                break;
            }
            checked = checked->next;
        }
        if (child != NULL) break;
        
        current_thread->status = SCHED_WAITING;
        spinlock_release(&scheduler_lock);
        reschedule();
    }
    // TODO: change when adding more wstatuses
    if (wstatus != NULL) *wstatus = child->exitcode;

    // unlink the child
    UNLINK_DOUBLE_LINKED_LIST(child, zombie_list)
    pid_t child_pid = child->pid;
    kfree(child);
    spinlock_release(&scheduler_lock);
    return child_pid;
}