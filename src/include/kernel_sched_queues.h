#ifndef SCHED_QUEUES_H
#define SCHED_QUEUES_H
#include "kernel_spinlock.h"

struct process_t;
struct thread_t;
struct __thread_queue_inner {
    struct process_t * parent_process;
    struct thread_t * thread;
    unsigned int magic_queue_value;
    struct __thread_queue_inner * prev;
    struct __thread_queue_inner * next;
};
struct thread_queue {
    struct __thread_queue_inner queue;
    spinlock_t queue_lock;
} typedef thread_queue_t;

// here so that thread_queue is already defined for sem_t
#include "kernel_sched.h"

#endif
