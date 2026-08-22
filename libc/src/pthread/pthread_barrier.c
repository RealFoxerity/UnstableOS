#include <pthread.h>
#include <string.h>
#include <UnstableOS/syscalls.h>
#include <UnstableOS/futex.h>
#include <unistd.h>
#include <errno.h>

int pthread_barrierattr_init(pthread_barrierattr_t *attr) {
    if (!attr)
        return EINVAL;
    memset(attr, 0, sizeof(pthread_barrierattr_t));
    return 0;
}
int pthread_barrierattr_destroy(pthread_barrierattr_t *attr) {
    return 0;
}

int pthread_barrier_init(pthread_barrier_t *restrict barrier,
    const pthread_barrierattr_t *restrict attr, unsigned count) {
    if (!barrier || count == 0)
        return EINVAL;
    if (attr)
        barrier->__attr = *attr;
    barrier->__counter = barrier->__requested_count = count;
    return 0;
}
int pthread_barrier_destroy(pthread_barrier_t *barrier) {
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
    if (!barrier)
        return EINVAL;
    if (barrier->__requested_count == 1)
        return PTHREAD_BARRIER_SERIAL_THREAD;

    while (1) {
        __atomic_compare_exchange(&barrier->__counter, &(unsigned long){0}, &barrier->__requested_count, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
        unsigned long expected = __atomic_load_n(&barrier->__counter, __ATOMIC_ACQUIRE);
        if (expected == 0)
            continue;

        unsigned long desired = expected - 1;
        if (__atomic_compare_exchange(
            &barrier->__counter,
            &expected, &desired, 0,
            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
        ) {
            if (desired == 0) {
                _syscall(SYSCALL_FUTEX, &barrier->__counter, FUTEX_WAKE, -1);
                return PTHREAD_BARRIER_SERIAL_THREAD;
            }

            int ret = 0;
            requeue:
            ret = _syscall(SYSCALL_FUTEX, &barrier->__counter, FUTEX_WAIT, desired, 0, NULL, 0);
            switch (ret) {
                case -EINTR:
                case -EAGAIN:
                    break;
                default:
                    return 0;
            }
            desired = __atomic_load_n(&barrier->__counter, __ATOMIC_ACQUIRE);
            // an entire barrier somehow happened in the meantime
            if (desired == 0 || desired > expected)
                return 0;
            goto requeue;
        }
    }
}

