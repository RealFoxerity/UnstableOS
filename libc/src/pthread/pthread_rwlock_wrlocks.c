#include <pthread.h>
#include <errno.h>

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    pthread_testcancel();
    return pthread_mutex_lock(&rwlock->__wlock);
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    pthread_testcancel();
    return pthread_mutex_trylock(&rwlock->__wlock);
}

int pthread_rwlock_clockwrlock(pthread_rwlock_t *restrict rwlock, clockid_t clock_id, const struct timespec *restrict abstime) {
    if (!rwlock || !abstime)
        return EINVAL;
    pthread_testcancel();
    return pthread_mutex_clocklock(&rwlock->__wlock, clock_id, abstime);
}
int pthread_rwlock_timedwrlock(pthread_rwlock_t *restrict rwlock, const struct timespec *restrict abstime) {
    return pthread_mutex_clocklock(&rwlock->__wlock, CLOCK_REALTIME, abstime);
}