#include "include/uthreads.h"
#include "include/stdlib.h"
#include "../../src/include/kernel.h"
#include <stdio.h>
#include <string.h>

// TODO: redo to not need    struct uthread_args * self

static inline void thread_wrapper(struct uthread_args * warg) {
    int retval = ((int(*)(struct uthread_args *, void*))warg->entry_point)(warg, warg->args);
    uthread_exit(warg, retval);
} // i apologise, but i mean, it works

uthread_t uthread_create(int (* entry_point)(struct uthread_args *, void*), void * arg) {
    mutex_t thread_mutex = mutex_init();
    if (thread_mutex < 0) return (uthread_t){.thread_lock=thread_mutex}; // errno

    struct uthread_args * wrapped_arguments = malloc(sizeof(struct uthread_args));
    if (!wrapped_arguments) {
        mutex_destroy(thread_mutex);
        return (uthread_t){0};
    }

    memset(wrapped_arguments, 0, sizeof(struct uthread_args));

    wrapped_arguments->entry_point = entry_point;
    wrapped_arguments->thread_lock = thread_mutex;
    wrapped_arguments->args = arg;

    mutex_lock(wrapped_arguments->thread_lock); 
    // we don't reschedule threads so in cases where uthread_join() is immediately 
    // after uthread_create() it would instantly join - not enough time for the wrapper to lock

    syscall(SYSCALL_CREATE_THREAD, thread_wrapper, wrapped_arguments);

    return (uthread_t) {
        .args = wrapped_arguments,
        .thread_lock = thread_mutex // duplicate, but more user friendly(?)
    };
}

int uthread_join(uthread_t thread) {
    mutex_lock(thread.thread_lock);
    printf("1");
    mutex_destroy(thread.thread_lock);
    int out = thread.args->exitcode; // avoid UAF
    free(thread.args);
    return out;
}

void uthread_exit(struct uthread_args * self, int exitcode) {
    self->exitcode = exitcode;
    printf("2");
    mutex_unlock(self->thread_lock);
    printf("3");
    syscall(SYSCALL_EXIT_THREAD);
    while(1); // to be safe
    __builtin_unreachable();
}

semaphore_t semaphore_init(int initial_value) {
    return syscall(SYSCALL_SEM_INIT, initial_value);
}

void semaphore_post(semaphore_t semaphore_id) {
    syscall(SYSCALL_SEM_POST, semaphore_id);
}

void semaphore_wait(semaphore_t semaphore_id) {
    syscall(SYSCALL_SEM_WAIT, semaphore_id);
}

void semaphore_destroy(semaphore_t semaphore_id) {
    syscall(SYSCALL_SEM_DESTROY, semaphore_id);
}

mutex_t mutex_init() {
    return semaphore_init(1);
}

void mutex_lock(mutex_t mutex_id) {
    semaphore_wait(mutex_id);
}

void mutex_unlock(mutex_t mutex_id) {
    semaphore_post(mutex_id);
}

void mutex_destroy(mutex_t mutex_id) {
    semaphore_destroy(mutex_id);
}
