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
                kprintf("Process %d deadlocked, killing...\n", pprocess->pid);
                pprocess->threads->status = SCHED_CLEANUP;
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

void spinlock_acquire(spinlock_t * lock) { // sets lock = 1, acquiring it
    if (!lock) panic("Tried to lock a NULL spinlock");

    asm volatile ("pushf; pop %0;" : "=R"(lock->eflags));

    asm volatile("sti");
    do {
        asm volatile ("pause");
    } while (!__atomic_compare_exchange_n(&lock->state, &(unsigned long){SPINLOCK_UNLOCKED}, SPINLOCK_LOCKED, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    asm volatile("cli");
}

// WARNING NO WAY TO DETECT DEADLOCKS FOR SPINLOCKING
// NEVER TRY TO LOCK A GLOBAL SPINLOCK INSIDE NONREENTRANT INTERRUPTS
void spinlock_acquire_nonreentrant(spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL spinlock");

    asm volatile ("pushf; pop %0;" : "=R"(lock->eflags));

    do {
        asm volatile ("pause");
    } while (!__atomic_compare_exchange_n(&lock->state, &(unsigned long){SPINLOCK_UNLOCKED}, SPINLOCK_LOCKED, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
}

void spinlock_release(spinlock_t * lock) {if (!lock) panic("Tried to release NULL spinlock"); __atomic_store_n(&lock->state, SPINLOCK_UNLOCKED, __ATOMIC_RELEASE); asm volatile ("push %0; popf;" :: "R"(lock->eflags));}

void kernel_sem_post(process_t * calling_process, int sem_idx) {
    __atomic_fetch_add(&calling_process->semaphores[sem_idx].value, 1, __ATOMIC_ACQUIRE);
    thread_queue_unblock(&calling_process->semaphores[sem_idx].waiting_queue);
}
void kernel_sem_wait(process_t * calling_process, thread_t * calling_thread, int sem_idx) {
    int old_val;
    while (1) { // basically check whether nothing decremented the value during our attempt, cmpxchg decrements in this case
        old_val = calling_process->semaphores[sem_idx].value;

        if (old_val > 0) {
            if (__atomic_compare_exchange_n(&calling_process->semaphores[sem_idx].value, (unsigned long *)&old_val, old_val - 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;
        } else {
            thread_queue_add(&calling_process->semaphores[sem_idx].waiting_queue, calling_process, calling_thread, SCHED_UNINTERR_SLEEP);
        }
    }
}