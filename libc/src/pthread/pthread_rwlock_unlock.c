#include <pthread.h>
#include <errno.h>


int pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;

    // unlock on non-locked object or is currently being locked by a different thread
    // we shouldn't meddle with this lock either way
    if (!rwlock->__wlock.__state)
        return 0;

    // we need to figure out whether this rwlock is locked for writes or reads
    // this is so dumb but unlocking when not the calling thread is UB anyway so this *should* be fine
    // this method provides some EPERM capabilities, but is not completely thread-safe in that UB scenario
    if (rwlock->__val == 0 || // normal write lock
        (rwlock->__val == 1 && rwlock->__vlock.__state) || // at least 1 rdlock waiting on its mutex_lock
        __atomic_sub_fetch(&rwlock->__val, 1, __ATOMIC_RELEASE) == 0 // normal read lock
    ) {
        return pthread_mutex_unlock(&rwlock->__wlock);
    }
    return 0;
}