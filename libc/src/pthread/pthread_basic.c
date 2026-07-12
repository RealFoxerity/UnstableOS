#include <pthread.h>
#include <UnstableOS/tls.h>
#include <UnstableOS/futex.h>
#include <UnstableOS/syscalls.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>

static size_t thread_count = 1;

pthread_t pthread_self() {
    return (pthread_t)__tls_get_tcb();
}

int pthread_equal(pthread_t t1, pthread_t t2) {
    if (t1 == t2)
        return 0;
    return 1; //?
}

__attribute__((noreturn)) void pthread_exit(void *value_ptr) {
    // run the dtors
    pthread_t us = pthread_self();
    us->__ret = value_ptr;

    if (__atomic_sub_fetch(&thread_count, 1, __ATOMIC_ACQUIRE) == 0)
        exit((long)value_ptr);

    _syscall(SYSCALL_EXIT_THREAD);
    __builtin_unreachable();
}

int pthread_detach(pthread_t thread) {
    if (thread == PTHREAD_NULL)
        return EINVAL;
    // dead thread, POSIX says zombies are alive I guess
    if (!__tls_get_tcb()->pcb->thread_slots[thread->__thread_slot])
        return ESRCH;
    if (thread->__detached == PTHREAD_CREATE_DETACHED)
        return EINVAL;

    thread->__detached = PTHREAD_CREATE_DETACHED;

    // for pthread_join() threads to awake
    _syscall(SYSCALL_FUTEX, &thread->__tid, FUTEX_WAKE, ULONG_MAX);
    return 0;
}

#include <stdio.h>
int pthread_join(pthread_t thread, void **value_ptr) {
    pthread_testcancel();

    if (thread == PTHREAD_NULL)
        return EINVAL;
    if (!pthread_equal(thread, pthread_self()))
        return EDEADLK;
    if (!__tls_get_tcb()->pcb->thread_slots[thread->__thread_slot])
        return ESRCH;
    if (thread->__detached == PTHREAD_CREATE_DETACHED)
        return EINVAL; // POSIX unspecified

    pid_t thread_id = thread->__tid;

    if (thread_id == 0) {
        joined:
        if (value_ptr)
            *value_ptr = thread->__ret;
        char expected = 1;
        // compare exchange to avoid races marking running threads as unused
        __atomic_compare_exchange_n(
            &__tls_get_tcb()->pcb->thread_slots[thread->__thread_slot],
            &expected, 0, 0,
            __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        return 0;
    }

    while (1) {
        // normal syscall, as pthread_join is a cancellation point
        long ret = syscall(SYSCALL_FUTEX, &thread->__tid, FUTEX_WAIT, thread_id, thread_id, NULL);
        if (ret == 0) {
            if (thread->__detached == PTHREAD_CREATE_DETACHED)
                return EINVAL; // POSIX unspecified
            if (thread->__tid == thread_id) continue; // otherwise spurious wakeup?
        }

        if (ret == 0 || ret == -EOWNERDEAD) {
            if (thread->__tid == 0)
                goto joined;

            // we somehow raced on join and on thread creation
            // this should not happen assuming the correct usage of pthread_join
            // however if it did, just return whatever is currently there as a dummy value
            fprintf(stderr,
"libc: Raced on pthread_join and pthread_create, are you using the function correctly?\n");
            goto joined;
        }
    }
}

struct wrapped_args {
    void *(*start_routine)(void*);
    void *arg;
};

static __attribute__((noreturn)) void pthread_create_wrapper(struct wrapped_args *args) {
    __atomic_add_fetch(&thread_count, 1, __ATOMIC_RELEASE);
    struct wrapped_args largs = *args; // to free as soon as possible
    free(args);
    pthread_exit(largs.start_routine(largs.arg));
}

int pthread_create(pthread_t *restrict thread,
       const pthread_attr_t *restrict attr,
       void *(*start_routine)(void*), void *restrict arg
) {
    if (thread == NULL || start_routine == NULL)
        return EINVAL;
    size_t guard_size = 0;
    if (attr != NULL)
        guard_size = attr->__guard_size;

    struct wrapped_args * args = malloc(sizeof(struct wrapped_args));
    if (args == NULL) return ENOMEM;

    args->start_routine = start_routine;
    args->arg = arg;

    // syscall because pthread_create is a cancellation point
    pthread_t new_thread = (pthread_t)syscall(SYSCALL_CREATE_THREAD, pthread_create_wrapper, args, guard_size);
    if (new_thread == NULL) {
        free(args);
        return EAGAIN;
    }
    // this whole situation is kinda bad, but the standard doesn't say whether detachment is atomic,
    // so I assume this is okay?

    if (attr != NULL)
        if (attr->__detached == PTHREAD_CREATE_DETACHED)
            pthread_detach(new_thread);

    *thread = new_thread;
    return 0;
}