#include <pthread.h>
#include <stddef.h>
#include <errno.h>

#include "UnstableOS/tls.h"

void pthread_testcancel() {
    pthread_t us = pthread_self();
    if (us->__cancelable == PTHREAD_CANCEL_ENABLE &&
        us->__cancel_pending
    )
        pthread_exit(NULL);
}

int pthread_setcancelstate(int state, int *oldstate) {
    if (oldstate == NULL)
        return EINVAL;

    pthread_t us = pthread_self();
    switch (state) {
        case PTHREAD_CANCEL_ENABLE:
        case PTHREAD_CANCEL_DISABLE:
            *oldstate = __atomic_exchange_n(&us->__cancelable, state, __ATOMIC_RELEASE);
            return 0;
        default:
            return EINVAL;
    }
}
int pthread_setcanceltype(int type, int *oldtype) {
    if (oldtype == NULL)
        return EINVAL;

    pthread_t us = pthread_self();
    switch (type) {
        case PTHREAD_CANCEL_DEFERRED:
        case PTHREAD_CANCEL_ASYNCHRONOUS:
            *oldtype = __atomic_exchange_n(&us->__cancelability_type, type, __ATOMIC_RELEASE);
            return 0;
        default:
            return EINVAL;
    }
}

int pthread_cancel(pthread_t thread) {
    if (thread == PTHREAD_NULL)
        return EINVAL;
    // dead thread, POSIX says zombies are alive I guess
    if (!__tls_get_tcb()->pcb->thread_slots[thread->__thread_slot])
        return ESRCH;
    thread->__cancel_pending = 1;
    return 0;
}