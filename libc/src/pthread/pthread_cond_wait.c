#include <pthread.h>
#include <unistd.h>
#include <UnstableOS/syscalls.h>
#include <UnstableOS/futex.h>
#include <errno.h>

static int __pthread_cond_clockwait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex, clockid_t clock_id,
       const struct timespec *restrict abstime, char check_time) {
    if (!cond || !mutex)
        return EINVAL;

    if (check_time) {
        if (!abstime)
            return EINVAL;
        if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
            return EINVAL;
        if (abstime->tv_sec < 0)
            return EINVAL;
        switch (clock_id) {
            case CLOCK_MONOTONIC:
            case CLOCK_REALTIME:
                break;
            default:
                return EINVAL;
        }
    }

    // POSIX says mutex has to be locked (by us) before invoking this function
    if (!mutex->__state || mutex->__owner != (unsigned long)pthread_self()->__tid)
        return EPERM;

    pthread_testcancel();

    int old_cancel_type;
    // Cancellability can allow the thread to just die,
    //   which would make any part of this function potentially deadlock the entire cond queue
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &old_cancel_type);

    int err = pthread_mutex_lock(&cond->__cond_lock);
    if (err) {
        // __cond_lock is not a robust mutex, so this will never leave the mutex in a locked state
        pthread_setcanceltype(old_cancel_type, NULL);
        return err;
    }

    pthread_mutex_unlock(mutex);

    unsigned long magic_val = __atomic_load_n(&cond->__magic, __ATOMIC_ACQUIRE);

    pthread_mutex_unlock(&cond->__cond_lock);

    // magic_val because a race could happen right here

    again:
    err = -_syscall(SYSCALL_FUTEX, &cond->__magic, FUTEX_WAIT, magic_val, 0, abstime, clock_id);

    switch (err) {
        case EINVAL:
        case EDEADLK:
        case ETIMEDOUT:
        case EAGAIN:  // magic was different, race between mutex unlock and futex, a different thread called FUTEX_WAKE
        case 0:       // normal futex wake up
            break;
        case EINTR:
            // could be EINTR because of cancel being raised
            if (pthread_self()->__cancelable == PTHREAD_CANCEL_ENABLE && pthread_self()->__cancel_pending)
                break;
        default:
            goto again;
    }

    int temp;
    // we could theoretically time out with ETIMEDOUT and fail the mutex at the same time
    // in that case, it's more important to notify the application about the mutex lock failure
    if ((temp = pthread_mutex_lock(mutex)))
        err = temp;

    pthread_setcanceltype(old_cancel_type, NULL);
    pthread_testcancel();
    return err;
}

int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex) {
    return __pthread_cond_clockwait(cond, mutex, 0, NULL, 0);
}

int pthread_cond_clockwait(pthread_cond_t *restrict cond,
       pthread_mutex_t *restrict mutex, clockid_t clock_id,
       const struct timespec *restrict abstime) {
    return __pthread_cond_clockwait(cond, mutex, clock_id, abstime, 1);
}

int pthread_cond_timedwait(pthread_cond_t *restrict cond,
       pthread_mutex_t *restrict mutex,
       const struct timespec *restrict abstime) {
    if (!cond)
        return EINVAL;
    return __pthread_cond_clockwait(cond, mutex, cond->__attr.__clockid, abstime, 1);
}