//void kernel_sem_init();
//void kernel_sem_destroy();
#include "include/kernel.h"
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

// WARNING NO WAY TO DETECT DEADLOCKS FOR SPINLOCKING
// NEVER TRY TO LOCK A GLOBAL SPINLOCK INSIDE NONREENTRANT INTERRUPTS
void spinlock_waiton(spinlock_t * lock) {while (lock->state != SPINLOCK_UNLOCKED) {asm volatile ("pause");}}
void spinlock_acquire(spinlock_t * lock) { // sets lock = 1, acquiring it
    #ifdef TARGET_I386 // TODO: test if actually works, i suspect it doesn't
        unsigned long prev_eflags;
        asm volatile ("pushf; pop %0;" : "=R"(prev_eflags));

        asm volatile("sti;");
        while (lock->state != SPINLOCK_UNLOCKED) reschedule();

        asm volatile("cli;"); // i386, no other cores, no races anywhere (pretty lonely here...)

        lock->state = SPINLOCK_LOCKED;

        asm volatile ("push %0; popf;" :: "R"(prev_eflags));

    #else
    while (1) {
        spinlock_waiton(lock);

        unsigned long current_state = SPINLOCK_UNLOCKED;
        asm volatile( // needed to not cause race between while and lock = 1
            "lock cmpxchgl %1, %2;" // if eax == value  ->  mov %1, %2; else mov value, eax
            : "+a"(current_state) : "r"(SPINLOCK_LOCKED), "m"(lock->state) : "memory"
        );
        if (!current_state) return;
    }
    #endif
}
void spinlock_release(spinlock_t * lock) {lock->state = SPINLOCK_UNLOCKED;}

void kernel_sem_post(process_t * calling_process, int sem_idx) {
    asm volatile (
        "lock incl (%0)"
    :: "R"(&calling_process->semaphores[sem_idx].value));
    thread_queue_unblock(&calling_process->semaphores[sem_idx].waiting_queue);
}
void kernel_sem_wait(process_t * calling_process, thread_t * calling_thread, int sem_idx) {
    #ifdef TARGET_I386
        unsigned long prev_eflags;
        asm volatile ("pushf; pop %0;" : "=R"(prev_eflags));
        asm volatile("cli;");

        if (calling_process->semaphores[sem_idx].value > 0)
            calling_process->semaphores[sem_idx].value --;
        else 
            thread_queue_add(&calling_process->semaphores[sem_idx].waiting_queue, calling_process, calling_thread, SCHED_UNINTERR_SLEEP);

        asm volatile ("push %0; popf;" :: "R"(prev_eflags));
    #else
    int old_val, old_val2, new_count;
    while (1) { // basically check whether nothing decremented the value during our attempt, cmpxchg decrements in this case
        old_val = calling_process->semaphores[sem_idx].value;
        old_val2 = old_val;

        new_count = old_val - 1;

        if (old_val > 0) {
            asm volatile(
                "lock cmpxchgl %1, %2;" // if eax == value  ->  mov new_count, value; else mov value, eax
                :"+a"(old_val)
                :"R"(new_count), "m"(calling_process->semaphores[sem_idx].value)
            );
            if (old_val == old_val2) {
                return;
            }
        } else {
            thread_queue_add(&calling_process->semaphores[sem_idx].waiting_queue, calling_process, calling_thread, SCHED_UNINTERR_SLEEP);
        }
    }
    #endif
}