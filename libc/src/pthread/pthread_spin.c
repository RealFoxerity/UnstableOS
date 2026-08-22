#include <pthread.h>
#include <string.h>
#include <errno.h>


int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
    if (lock == NULL)
        return EINVAL;
    memset(lock, 0, sizeof(pthread_spinlock_t));
    return 0;
}
int pthread_spin_destroy(pthread_spinlock_t *lock) {
    return 0;
}

union spinlock_owner { // keep the same as in types.h, needed for cmpxchg
    struct {
        unsigned long __owner : 31;
        unsigned long __locked : 1;
    };
    pid_t __ownerx;
};

int pthread_spin_lock(pthread_spinlock_t *lock) {
    if (lock == NULL)
        return EINVAL;
    union spinlock_owner expected = {0};
    union spinlock_owner wanted = {
        .__owner = (unsigned long)pthread_self()->__tid,
        .__locked = 1
    };

    // intended aliasing
    if (wanted.__ownerx == lock->__ownerx)
        return EDEADLK;

    while (lock->__locked || !__atomic_compare_exchange(
        &lock->__ownerx,
        &expected.__ownerx, &wanted.__ownerx,
        0,
        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            asm volatile ("pause");

    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
    if (lock == NULL)
        return EINVAL;
    union spinlock_owner expected = {0};
    union spinlock_owner wanted = {
        .__owner = (unsigned long)pthread_self()->__tid,
        .__locked = 1
    };

    // intended aliasing
    if (wanted.__ownerx == lock->__ownerx)
        return EDEADLK;

    if (lock->__locked || !__atomic_compare_exchange(
        &lock->__ownerx,
        &expected.__ownerx, &wanted.__ownerx,
        0,
        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return EBUSY;
    return 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
    if (lock == NULL)
        return EINVAL;
    union spinlock_owner expected = {0};
    union spinlock_owner wanted = {
        .__owner = (unsigned long)pthread_self()->__tid,
        .__locked = 1
    };

    // intended aliasing
    if (wanted.__ownerx != lock->__ownerx)
        return EPERM;

    __atomic_store_n(&lock->__ownerx, 0, __ATOMIC_RELEASE);
    return 0;
}