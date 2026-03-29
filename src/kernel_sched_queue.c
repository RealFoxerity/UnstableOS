#include "include/kernel.h"
#include "include/kernel_sched.h"
#include "../libc/src/include/string.h"
#include "include/kernel_spinlock.h"
#include <stdint.h>

static void __thread_queue_unblock(thread_queue_t * thread_queue) {
    if (thread_queue->queue.parent_process == NULL) { // queue with no waiting processes
        return;
    }


    struct __thread_queue_inner * next = thread_queue->queue.next;

    kassert(thread_queue->queue.thread->instances > 0);

    char rerun = 0;
    // see the comment in kernel_sched.h
    if (thread_queue->queue.magic_queue_value == thread_queue->queue.thread->magic_queue_value) {
        thread_queue->queue.thread->status = SCHED_RUNNABLE;
    } else {
        rerun = 1;
    }
    if (__atomic_sub_fetch(&thread_queue->queue.thread->instances, 1, __ATOMIC_RELAXED) == 0) kfree(thread_queue->queue.thread);

    if (next != NULL) {
        memcpy(&thread_queue->queue, thread_queue->queue.next, sizeof(struct __thread_queue_inner));
        thread_queue->queue.prev = next->prev;
        kfree(next);
    } else {
        memset(&thread_queue->queue, 0, sizeof(struct __thread_queue_inner));
    }

    // the thread handle was invalid, rerun to unblock another thread
    if (rerun) __thread_queue_unblock(thread_queue);
}

void thread_queue_unblock(thread_queue_t * thread_queue) {
    spinlock_acquire(&thread_queue->queue_lock);
    __thread_queue_unblock(thread_queue);
    spinlock_release(&thread_queue->queue_lock);
    reschedule();
}

void thread_queue_unblock_all(thread_queue_t * thread_queue) {
    spinlock_acquire(&thread_queue->queue_lock);
    while (thread_queue->queue.parent_process != NULL)
        __thread_queue_unblock(thread_queue);
    spinlock_release(&thread_queue->queue_lock);
    reschedule();
}

// pprocess and thread here to allow adding other threads than current
void thread_queue_add(thread_queue_t * thread_queue, process_t * pprocess, thread_t * thread, enum pstatus_t new_status) {
    spinlock_acquire(&thread_queue->queue_lock);
    if (__atomic_add_fetch(&thread->instances, 1, __ATOMIC_RELAXED) == UINT32_MAX) panic("Overflown thread instance count!");

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

    thread_queue->queue.magic_queue_value = __atomic_add_fetch(&thread->magic_queue_value, 1, __ATOMIC_RELAXED);

    before_unlock:
    thread->status = new_status;
    spinlock_release(&thread_queue->queue_lock);

    //kprintf("sleeping on thread id %d of process %d\n", thread->tid, pprocess->pid);

    reschedule();
}