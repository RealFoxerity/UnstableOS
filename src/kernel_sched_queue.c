#include "include/kernel.h"
#include "include/kernel_sched.h"
#include "include/kernel_semaphore.h"
#include "../libc/src/include/string.h"

void thread_queue_unblock(thread_queue_t * thread_queue) {
    spinlock_acquire(&thread_queue->queue_lock);
    if (thread_queue->queue.parent_process == NULL) { // queue with no waiting processes
        spinlock_release(&thread_queue->queue_lock);
        return;
    }


    struct __thread_queue_inner * next = thread_queue->queue.next;
    
    thread_queue->queue.thread->status = SCHED_RUNNABLE;

    if (next != NULL) {
        memcpy(&thread_queue->queue, thread_queue->queue.next, sizeof(struct __thread_queue_inner));
        thread_queue->queue.prev = next->prev;
        kfree(next);
    } else {
        memset(&thread_queue->queue, 0, sizeof(struct __thread_queue_inner));
    }

    spinlock_release(&thread_queue->queue_lock);

    unsigned long prev_eflags;
    asm volatile ("pushf; pop %0;" : "=R"(prev_eflags));
    asm volatile("sti"); // have to enable interrupts for reschedule()
    reschedule();
    asm volatile ("push %0; popf;" :: "R"(prev_eflags));
}

// pprocess and thread here to allow adding other threads than current
void thread_queue_add(thread_queue_t * thread_queue, process_t * pprocess, thread_t * thread, enum pstatus_t new_status) {
    spinlock_acquire(&thread_queue->queue_lock);

    if (thread_queue->queue.parent_process == NULL) {
        thread_queue->queue.parent_process = pprocess;
        thread_queue->queue.thread = thread;
        thread_queue->queue.prev = &thread_queue->queue;
        goto before_unlock;
    }

    thread_queue->queue.prev->next = kalloc(sizeof(struct __thread_queue_inner));
    if (!thread_queue->queue.prev->next) panic("Not enough memory to extend thread queue!");
    memset(thread_queue->queue.prev->next, 0, sizeof(struct __thread_queue_inner));

    thread_queue->queue.prev->next->prev = thread_queue->queue.prev;
    thread_queue->queue.prev = thread_queue->queue.prev->next;


    thread_queue->queue.prev->parent_process = pprocess;
    thread_queue->queue.prev->thread = thread;


    before_unlock:
    spinlock_release(&thread_queue->queue_lock);

    thread->inside_kernel = 1; // we are going to reschedule and the scheduler needs to know if we got interrupted in ring 0 or not
    thread->status = new_status;
    kprintf("sleeping on thread id %d of process %d\n", thread->tid, pprocess->pid);


    unsigned long prev_eflags;
    asm volatile ("pushf; pop %0;" : "=R"(prev_eflags));
    asm volatile("sti"); // have to enable interrupts for reschedule()
    reschedule();
    asm volatile ("push %0; popf;" :: "R"(prev_eflags));
}