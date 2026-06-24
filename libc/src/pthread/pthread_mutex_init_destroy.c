#include <pthread.h>
#include <errno.h>
#include <stddef.h>

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    if (attr == NULL)
        return EINVAL;

    *attr = (pthread_mutexattr_t) {0};
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *restrict mutex, const pthread_mutexattr_t *restrict attr) {
    if (mutex == NULL)
        return EINVAL;

    *mutex = PTHREAD_MUTEX_INITIALIZER;
    if (attr)
        mutex->__attr = *attr;

    return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    return 0;
}