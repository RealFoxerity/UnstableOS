#include <pthread.h>
#include <errno.h>
#include <stddef.h>

int pthread_attr_init(pthread_attr_t *attr) {
    if (attr == NULL)
        return EINVAL;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    if (attr == NULL)
        return EINVAL;
    *attr = (pthread_attr_t){0};
    return 0;
}


int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate) {
    if (attr == NULL || detachstate == NULL)
        return EINVAL;
    *detachstate = attr->__detached;
    return 0;
}
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
    if (attr == NULL)
        return EINVAL;
    switch (detachstate) {
        case PTHREAD_CREATE_DETACHED:
        case PTHREAD_CREATE_JOINABLE:
            attr->__detached = detachstate;
            return 0;
        default:
            return EINVAL;
    }
}

// keep the same as in kernel_sched.h
#include <limits.h>
#define PROGRAM_STACK_GUARD_MAX ((1<<20) - PAGE_SIZE)

int pthread_attr_getguardsize(const pthread_attr_t *restrict attr, size_t *restrict guardsize) {
    if (attr == NULL || guardsize == NULL)
        return EINVAL;
    *guardsize = attr->__guard_size;
    return 0;
}
int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize) {
    if (attr == NULL)
        return EINVAL;
    if (guardsize > PROGRAM_STACK_GUARD_MAX)
        return EINVAL;
    attr->__guard_size = guardsize;
    return 0;
}