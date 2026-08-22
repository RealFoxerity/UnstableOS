#include <pthread.h>
#include <errno.h>

#include <string.h>

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr) {
    if (!attr)
        return EINVAL;
    memset(attr, 0, sizeof(pthread_rwlockattr_t));
    return 0;
}
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr) {
    return 0;
}
