#include "kernel_sched.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <UnstableOS/futex.h>
spinlock_t futex_lock = {0};

long futex_wait(const uint32_t * wait_addr, uint32_t expected, pid_t owner, struct timespec * timeout) {
    if (owner < 0) return -EINVAL;
    if (current_process->threads->next == NULL)
        return -EDEADLK;
    if (owner > 0) {
        spinlock_acquire(&scheduler_lock);
        char found_owner = 0;
        for (thread_t * thread = current_process->threads; thread != NULL; thread = thread->next) {
            if (thread->tid == owner) {
                found_owner = 1;
                break;
            }
        }
        spinlock_release(&scheduler_lock);
        if (!found_owner)
            return -EOWNERDEAD;
    }

    char is_kernel = current_process->pid == 0;
    if (timeout && !paging_check_address_range(timeout, sizeof(struct timespec), 0, is_kernel))
        return -EFAULT;

    // owner check
    if (timeout && timeout->tv_nsec == 0 && timeout->tv_sec == 0)
        return 0;

    // disable interrupts so we don't race in early nanosleep
    // spinlock_release restores interrupts
    asm volatile ("cli;");

    spinlock_acquire(&futex_lock);
    // this value is supposed to only guard against futex wait/wake races
    // since we always lock the futex lock, doing it like this is okay
    // (aka with correct usage the race condition doesn't exist)
    if (*wait_addr != expected) {
        spinlock_release(&futex_lock);
        return -EAGAIN;
    }

    current_thread->owner_dead = 0;
    current_thread->futex_owner = owner;
    current_thread->futex_addr = wait_addr;
    current_thread->is_waiting_on_futex = 1;
    if (timeout) {
        spinlock_release(&futex_lock);
        // interrupts disabled here because of the earlier cli

        // nanosleep has an internal reschedule() which reenables them
        long slept = sys_nanosleep(current_process, current_thread, *timeout, NULL);
        current_thread->is_waiting_on_futex = 0;
        // however the reschedule() resets the flags back to disabled interrupts...
        asm volatile("sti;");

        if (slept == 0)
            return -ETIMEDOUT;
        if (slept == -EINTR) {
            if (current_thread->sa_to_be_handled)
                return -EINTR;
            if (current_thread->owner_dead && owner)
                return -EOWNERDEAD;
            return 0;
        }
        return slept;
    }

    current_thread->status = SCHED_INTERR_SLEEP;
    spinlock_release(&futex_lock);
    reschedule();

    current_thread->is_waiting_on_futex = 0;
    asm volatile("sti;");

    if (current_thread->sa_to_be_handled)
        return -EINTR;
    return 0;
}

long futex_wake(const uint32_t * wait_addr, uint32_t wakeup_count) {
    if (wakeup_count == 0) return 0;
    if (wakeup_count > LONG_MAX) wakeup_count = LONG_MAX;

    spinlock_acquire(&futex_lock);
    spinlock_acquire(&scheduler_lock);

    long waked_up_threads = 0;
    for (thread_t * thread = current_process->threads; thread != NULL; thread = thread->next) {
        if (thread->is_waiting_on_futex && thread->futex_addr == wait_addr) {
            thread->is_waiting_on_futex = 0;
            thread->status = SCHED_RUNNABLE;
            waked_up_threads++;
            if (waked_up_threads >= wakeup_count)
                break;
        }
    }

    spinlock_release(&scheduler_lock);
    spinlock_release(&futex_lock);

    return waked_up_threads;
}

long sys_futex(const uint32_t * wait_addr, int op, uint32_t val, pid_t owner, struct timespec * timeout) {
    if ((long)wait_addr % 4)
        return -EINVAL;
    char is_kernel = current_process->pid == 0;
    if (!paging_check_address_range(wait_addr, sizeof(uint32_t), 0, is_kernel))
        return -EFAULT;

    switch (op) {
        case FUTEX_WAIT:
            return futex_wait(wait_addr, val, owner, timeout);
        case FUTEX_WAKE:
            return futex_wake(wait_addr, val);
        default:
            return -EINVAL;
    }
}

void futex_wake_owner_dead(const process_t * parent, pid_t tid) {
    kassert(parent);
    for (thread_t * thread = parent->threads; thread != NULL; thread = thread->next) {
        if (thread->is_waiting_on_futex &&
            thread->futex_owner == tid)
        {
            thread->owner_dead = 1;
            thread->is_waiting_on_futex = 0;
            thread->status = SCHED_RUNNABLE;
        }
    }
}