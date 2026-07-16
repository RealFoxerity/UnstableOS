//void kernel_sem_init();
//void kernel_sem_destroy();
#include "include/kernel.h"
#include "include/kernel_interrupts.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "include/mm/kernel_memory.h"
#include <string.h>
#include "include/kernel_semaphore.h"

#define kprintf(fmt, ...) kprintf("Kernel Sync: "fmt, ##__VA_ARGS__)

static inline void check_deadlock(sem_t * sem, process_t * pprocess) { // tested working for unnamed (i.e. same thread) semaphores
    if (sem->waiting_queue.queue.parent_process == NULL &&
        pprocess->threads->next == NULL) { // process has only 1 thread and that thread is now waiting on semaphore
            deadlock:
            asm volatile("cli;");
            kprintf("Encountered deadlock\n");

            if (pprocess->pid == 0) {
                panic("Kernel deadlocked");
                __builtin_unreachable();
            } else if (pprocess->pid == 1) {
                panic("Init deadlocked");
                __builtin_unreachable();
            } else {
                kprintf("Process %lu deadlocked, killing...\n", pprocess->pid);
                pprocess->do_cleanup = 1;
                reschedule();
            }
            return;
        }

    size_t thread_count = 0;
    thread_t * thread = pprocess->threads;
    while (thread != NULL) {
        thread_count++;
        thread = thread->next;
    }

    size_t thread_waiting_count = 0;
    struct __thread_queue_inner * tq = &sem->waiting_queue.queue;
    while (tq != NULL) {
        if (tq->parent_process == pprocess)
            thread_waiting_count ++;
        tq = tq->next;
    }

    if (thread_waiting_count == thread_count - 1) { // -1 because we didn't yet add the new thread
        goto deadlock;
    }
    return;
}

void spinlock_acquire(spinlock_t * lock) { // disables interrupts after lock
    if (!lock) panic("Tried to lock a NULL spinlock");
    CRIT_SEC_START

    asm volatile ("pushf; pop %0;" : "=R"(lock->eflags));

    // current process being null is an easy way to tell if the scheduler is initialized
    // before scheduler, we don't have interrupts and enabling them wouldn't be good
    if (!current_process) {
        lock->state = SPINLOCK_LOCKED;
        return;
    }

    asm volatile("cli");
    if (__atomic_compare_exchange_n(&lock->state, &(unsigned long){SPINLOCK_UNLOCKED}, SPINLOCK_LOCKED, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return; // in case we don't even need the sti

    do {
        //asm volatile ("pause");
        reschedule();
    } while (lock->state != SPINLOCK_UNLOCKED || !__atomic_compare_exchange_n(&lock->state, &(unsigned long){SPINLOCK_UNLOCKED}, SPINLOCK_LOCKED, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
}

// same as normal spinlock_acquire, but doesn't call cli
void spinlock_acquire_interruptible(spinlock_t * lock) {
    spinlock_acquire(lock);
    asm volatile("sti;");
}

// WARNING NO WAY TO DETECT DEADLOCKS FOR SPINLOCKING
// NEVER TRY TO LOCK A GLOBAL SPINLOCK INSIDE NONREENTRANT INTERRUPTS
void spinlock_acquire_nonreentrant(spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL spinlock");
    CRIT_SEC_START

    asm volatile ("pushf; pop %0;" : "=R"(lock->eflags));

    if (!current_process) {
        lock->state = SPINLOCK_LOCKED;
        return;
    }

    do {
        asm volatile ("pause");
    } while (!__atomic_compare_exchange_n(&lock->state, &(unsigned long){SPINLOCK_UNLOCKED}, SPINLOCK_LOCKED, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
}

void spinlock_release(spinlock_t * lock) {if (!lock) panic("Tried to release NULL spinlock"); CRIT_SEC_END __atomic_store_n(&lock->state, SPINLOCK_UNLOCKED, __ATOMIC_RELEASE); asm volatile ("push %0; popf;" :: "R"(lock->eflags));}


// maybe we don't need the vlock?

void rw_spinlock_acquire_read(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    spinlock_acquire_interruptible(&lock->vlock);

    if (__atomic_add_fetch(&lock->value, 1, __ATOMIC_ACQUIRE) == 1) {
        spinlock_acquire_interruptible(&lock->wlock);
        CRIT_SEC_END
    }

    spinlock_release(&lock->vlock);
}

void rw_spinlock_release_read(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    spinlock_acquire_interruptible(&lock->vlock);

    if (lock->value == 0)
        panic("Tried to release a read rw spinlock with 0 instances");

    if (__atomic_sub_fetch(&lock->value, 1, __ATOMIC_RELEASE) == 0) {
        CRIT_SEC_START
        spinlock_release(&lock->wlock);
    }

    spinlock_release(&lock->vlock);
}

void rw_spinlock_acquire_write(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    spinlock_acquire_interruptible(&lock->wlock);
}

void rw_spinlock_release_write(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    spinlock_release(&lock->wlock);
}

void kernel_sem_post(process_t * calling_process, int sem_idx) {
    kassert(calling_process->semaphores[sem_idx]);

    __atomic_fetch_add(&calling_process->semaphores[sem_idx]->value, 1, __ATOMIC_ACQUIRE);
    thread_queue_unblock(&calling_process->semaphores[sem_idx]->waiting_queue);
}
void kernel_sem_wait(process_t * calling_process, thread_t * calling_thread, int sem_idx) {
    kassert(calling_process->semaphores[sem_idx]);

    int old_val;
    while (1) { // basically check whether nothing decremented the value during our attempt, cmpxchg decrements in this case
        old_val = calling_process->semaphores[sem_idx]->value;

        if (old_val > 0) {
            if (__atomic_compare_exchange_n(&calling_process->semaphores[sem_idx]->value, (unsigned long *)&old_val, old_val - 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;
        } else {
            thread_queue_add(&calling_process->semaphores[sem_idx]->waiting_queue, calling_process, calling_thread, SCHED_UNINTERR_SLEEP);
        }
    }
}