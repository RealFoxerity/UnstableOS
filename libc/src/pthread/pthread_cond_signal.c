#include <pthread.h>
#include <unistd.h>
#include <UnstableOS/syscalls.h>
#include <UnstableOS/futex.h>
#include <errno.h>

static int __pthread_cond_wake_n(pthread_cond_t *cond, unsigned long n) {
    if (!cond)
        return EINVAL;
    __atomic_add_fetch(&cond->__magic, 1, __ATOMIC_RELEASE);
    _syscall(SYSCALL_FUTEX, &cond->__magic, FUTEX_WAKE, n);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    return __pthread_cond_wake_n(cond, -1);
}
int pthread_cond_signal(pthread_cond_t *cond) {
    return __pthread_cond_wake_n(cond, 1);
}