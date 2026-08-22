#include <pthread.h>
#include <errno.h>
#include <string.h>


int pthread_rwlock_init(pthread_rwlock_t *restrict rwlock, const pthread_rwlockattr_t *restrict attr) {
    if (!rwlock)
        return EINVAL;

    *rwlock = PTHREAD_RWLOCK_INITIALIZER;
    if (attr)
        rwlock->__attr = *attr;

    return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
    return 0;
}
