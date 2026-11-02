#include "kernel.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"

#include <string.h>
#include <errno.h>
#include <UnstableOS/halt.h>

int sys_halt(int how) {
    if (current_process->euid != 0)
        return -EPERM;

    static spinlock_t halt_spinlock = {0};
    spinlock_acquire(&halt_spinlock);

    if (how & HALT_FORCE) {
        halt:
        if (how & HALT_RESET) {
            kprintf("\n\nThe system will now reset!\n");
            kernel_reset_system();
        } else {
            kprintf("\n\nThe system will now halt!\n");
        }
        scheduler_lock.state = SPINLOCK_LOCKED;
        asm volatile ("cli; 1: hlt; jmp 1b");
        __builtin_unreachable();
    }

    spinlock_acquire(&scheduler_lock);
    init_task = kernel_task;
    spinlock_release(&scheduler_lock);

    while (process_list->next && (current_process == kernel_task || process_list->next->next)) {
        spinlock_acquire(&scheduler_lock);
        for (process_t * proc = process_list; proc; proc = proc->next) {
            if (proc != kernel_task && proc != current_process)
                proc->do_cleanup = 1;
        }
        spinlock_release(&scheduler_lock);
        reschedule();
    }

    while (current_process != kernel_task && current_process->threads->next) {
        if (current_process->threads != current_thread)
            current_process->threads->status = SCHED_THREAD_CLEANUP;
        else
            current_process->threads->next->status = SCHED_THREAD_CLEANUP;
        reschedule();
    }

    munmap_all(current_process);

    // at this point only this calling thread and the kernel should be alive

    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        inode_t * ino = kernel_inodes[i];
        if (!ino)
            continue;
        __atomic_store_n(&ino->is_mountpoint, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&ino->instances, 1, __ATOMIC_RELEASE);
        close_inode(ino);
    }

    superblock_t * sb = mounts;
    while (sb) {
        if (sb->funcs && sb->funcs->fs_deinit)
            sb->funcs->fs_deinit(sb);
        sb = sb->next;
    }

    kprintf("\n\nSyncing...\n");
    extern void hd_cache_flush();
    hd_cache_flush();
    goto halt;
}