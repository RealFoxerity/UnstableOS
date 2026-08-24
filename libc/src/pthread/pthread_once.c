#include <pthread.h>
#include <UnstableOS/futex.h>
#include <UnstableOS/syscalls.h>
#include <unistd.h>
#include <errno.h>
#define UNINITIALIZED 0
#define RUNNING       1
#define CONTENDED     2
#define INITIALIZED   3

static void __pthread_once_restore(void * control) {
    if (__atomic_exchange_n((pthread_once_t *)control, UNINITIALIZED, __ATOMIC_RELEASE) == CONTENDED)
        _syscall(SYSCALL_FUTEX, control, FUTEX_WAKE, -1);
}
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    // couldn't be bothered to do more properly
    if (!once_control)
        return EINVAL;

    if (*once_control == INITIALIZED)
        return 0;

    while (1) {
        pthread_once_t expected = UNINITIALIZED;
        if (__atomic_compare_exchange_n(
            once_control, &expected, RUNNING,
            0,
            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
        ) {
            pthread_cleanup_push(__pthread_once_restore, once_control);
            if (init_routine)
                init_routine();
            pthread_cleanup_pop(0);
            if (__atomic_exchange_n(once_control, INITIALIZED, __ATOMIC_RELEASE) == CONTENDED)
                _syscall(SYSCALL_FUTEX, once_control, FUTEX_WAKE, -1);
            return 0;
        }
        switch (expected) {
            case INITIALIZED:
                return 0;
            case RUNNING:
                __atomic_compare_exchange_n(
                    once_control, &expected, CONTENDED,
                    0,
                    __ATOMIC_RELEASE, __ATOMIC_RELAXED);
            case CONTENDED:
                _syscall(SYSCALL_FUTEX, once_control, FUTEX_WAIT, CONTENDED, 0, NULL, 0);
            default:
                continue;
        }
    }
}