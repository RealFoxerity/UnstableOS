#ifndef UTHREADS_H
#define UTHREADS_H

typedef long semaphore_t;
typedef long mutex_t;

struct uthread_args {
    int (*entry_point)(struct uthread_args *, void*);
    mutex_t thread_lock;
    int exitcode;
    void * args;
};
struct uthread_t {
    struct uthread_args * args;
    mutex_t thread_lock;
} typedef uthread_t;

uthread_t uthread_create(int (* entry_point)(struct uthread_args *, void*), void * arg); // mutex_t here to avoid deadlocks when early exiting a thread without delegating thread joining to the kernel
int uthread_join(uthread_t thread);
void __attribute__((noreturn)) uthread_exit(struct uthread_args * self, int exitcode);


semaphore_t semaphore_init(int initial_value);
void semaphore_post(semaphore_t semaphore_id);
void semaphore_wait(semaphore_t semaphore_id);
void semaphore_destroy(semaphore_t semaphore_id);

mutex_t mutex_init();
void mutex_lock(mutex_t mutex_id);
void mutex_unlock(mutex_t mutex_id);
void mutex_destroy(mutex_t mutex_id);
#endif