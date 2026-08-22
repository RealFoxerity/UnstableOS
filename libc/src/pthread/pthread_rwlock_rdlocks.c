#include <pthread.h>
#include <errno.h>
#include <limits.h>

// sorry for the code duplication

// too lazy to handle all possible races here
#define MAX_RWLOCK_VALUE (ULONG_MAX - PTHREAD_THREADS_MAX - 1)

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    pthread_testcancel();
    int ret = pthread_mutex_lock(&rwlock->__vlock);
    if (ret)
        return ret;

    if (__atomic_load_n(&rwlock->__val, __ATOMIC_ACQUIRE) >= MAX_RWLOCK_VALUE) {
        pthread_mutex_unlock(&rwlock->__vlock);
        return EAGAIN;
    }

    if (__atomic_add_fetch(&rwlock->__val, 1, __ATOMIC_ACQUIRE) == 1) {
        ret = pthread_mutex_lock(&rwlock->__wlock);
        if (ret) {
            __atomic_sub_fetch(&rwlock->__val, 1, __ATOMIC_RELEASE);
        }
    }

    pthread_mutex_unlock(&rwlock->__vlock);
    return ret;
}
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    pthread_testcancel();
    int ret = pthread_mutex_trylock(&rwlock->__vlock);
    if (ret)
        return ret;

    if (__atomic_load_n(&rwlock->__val, __ATOMIC_ACQUIRE) >= MAX_RWLOCK_VALUE) {
        pthread_mutex_unlock(&rwlock->__vlock);
        return EAGAIN;
    }

    if (__atomic_add_fetch(&rwlock->__val, 1, __ATOMIC_ACQUIRE) == 1) {
        ret = pthread_mutex_trylock(&rwlock->__wlock);
        if (ret) {
            __atomic_sub_fetch(&rwlock->__val, 1, __ATOMIC_RELEASE);
        }
    }

    pthread_mutex_unlock(&rwlock->__vlock);
    return ret;
}

int pthread_rwlock_clockrdlock(pthread_rwlock_t *restrict rwlock, clockid_t clock_id, const struct timespec *restrict abstime) {
    if (!rwlock || !abstime)
        return EINVAL;
    pthread_testcancel();
    int ret = pthread_mutex_clocklock(&rwlock->__vlock, clock_id, abstime);
    if (ret)
        return ret;

    if (__atomic_load_n(&rwlock->__val, __ATOMIC_ACQUIRE) >= MAX_RWLOCK_VALUE) {
        pthread_mutex_unlock(&rwlock->__vlock);
        return EAGAIN;
    }

    if (__atomic_add_fetch(&rwlock->__val, 1, __ATOMIC_ACQUIRE) == 1) {
        ret = pthread_mutex_clocklock(&rwlock->__wlock, clock_id, abstime);
        if (ret) {
            __atomic_sub_fetch(&rwlock->__val, 1, __ATOMIC_RELEASE);
        }
    }

    pthread_mutex_unlock(&rwlock->__vlock);
    return ret;
}
int pthread_rwlock_timedrdlock(pthread_rwlock_t *restrict rwlock, const struct timespec *restrict abstime) {
    return pthread_rwlock_clockrdlock(rwlock, CLOCK_REALTIME, abstime);
}