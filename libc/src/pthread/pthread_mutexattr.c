#include <pthread.h>
#include <errno.h>
#include <stddef.h>

int pthread_mutexattr_gettype(const pthread_mutexattr_t *restrict attr, int *restrict type) {
    if (attr == NULL || type == NULL)
        return EINVAL;

    *type = attr->__type;
    return 0;
}
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    if (attr == NULL)
        return EINVAL;

    switch (type) {
        case PTHREAD_MUTEX_NORMAL:
        case PTHREAD_MUTEX_ERRORCHECK:
        case PTHREAD_MUTEX_RECURSIVE:
        //case PTHREAD_MUTEX_DEFAULT:
            break;
        default:
            return EINVAL;
    }
    attr->__type = type;
    return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t *restrict attr, int *restrict robust) {
    if (attr == NULL || robust == NULL)
        return EINVAL;

    *robust = attr->__robust;
    return 0;
}
int pthread_mutexattr_setrobust(pthread_mutexattr_t *attr, int robust) {
    if (attr == NULL)
        return EINVAL;

    switch (robust) {
        case PTHREAD_MUTEX_STALLED:
        case PTHREAD_MUTEX_ROBUST:
            break;
        default:
            return EINVAL;
    }
    attr->__robust = robust;
    return 0;
}