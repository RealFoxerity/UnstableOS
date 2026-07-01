#include <stddef.h>
#include <pthread.h>
#include <UnstableOS/syscalls.h>
#include <UnstableOS/futex.h>
#include <UnstableOS/tls.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include <stdlib.h>

#define PTHREAD_MUTEX_RECURSIVE_MAX LONG_MAX // LONG because we lost that one bit to contention

// we basically use the owner field as a mutex for the mutex

int pthread_mutex_consistent(pthread_mutex_t *mutex) {
    if (mutex == NULL || !mutex->__attr.__robust || !mutex->__inconsistent)
        return EINVAL;
    mutex->__inconsistent = 0;
    return 0;
}


union mutex_owner { // keep the same as in types.h, needed for cmpxchg
    struct {
        unsigned long __owner : 31;
        unsigned long __contended : 1;
    };
    pid_t __ownerx;
};

#define PTHREAD_SPINAMOUNT 1000
extern char is_klibc;
int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (is_klibc) return 0;
    int error = pthread_mutex_trylock(mutex);
    switch (error) {
        case 0:
        case EDEADLK:
        case EOWNERDEAD:
        case ENOTRECOVERABLE:
        case EAGAIN:
            return error;
        default: break;
    }
    // kinda useless because we're not doing SMP, but good for the future
    // idea is that the other thread may just be adjusting a few fields
    for (int i = 0; i < PTHREAD_SPINAMOUNT; i++)
        asm volatile("pause");

    while ((error = pthread_mutex_trylock(mutex)) != 0) {
        switch (error) {
            case EDEADLK:
            case EOWNERDEAD:
            case ENOTRECOVERABLE:
            case EAGAIN:
                return error;
            default: break;
        }
        union mutex_owner owner;
        owner.__ownerx = mutex->__ownerx;
        owner.__contended = 1;

        pid_t expected = mutex->__ownerx;

        if (__atomic_compare_exchange(
            &mutex->__ownerx, &expected,
            &owner.__ownerx, 0,
            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
        ) {
            // another potential race? this time with pthread_mutex_unlock()?
            if (syscall(SYSCALL_FUTEX,
                    &mutex->__ownerx, FUTEX_WAIT,
                    mutex->__ownerx, mutex->__owner, NULL) == -EOWNERDEAD
            ) {
                mutex->__inconsistent = 1;
                mutex->__contended = 0;
                // remember, EOWNERDEAD is acquirable, and is done so by trylock
            }
        }
    }
    return 0;
}

// TODO: i guess rewrite when futex robust lists?
int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (is_klibc) return 0;
    if (mutex == NULL)
        return EINVAL;

    // according to POSIX, this function is supposed to acquire the lock, but return EOWNERDEAD
    if (/*mutex->__inconsistent ||*/ mutex->__unrecoverable) {
        /*mutex->__unrecoverable = 1;
        if (mutex->__contended)
            syscall(SYSCALL_FUTEX, &mutex->__ownerx, FUTEX_WAKE, ULONG_MAX);

        mutex->__contended = 0;*/
        return ENOTRECOVERABLE;
    }

    pid_t us = pthread_self()->__tid;
    struct thread_control_block * our_tcb = __tls_get_tcb();
    union mutex_owner expected;
    expected.__ownerx = 0;

    char is_dead = 0;
    docas:
    if (__atomic_compare_exchange(&mutex->__ownerx,
        &expected.__ownerx,
        &us,
        0,
        __ATOMIC_ACQUIRE,
        __ATOMIC_RELAXED
        )) {
        // TODO: this is the somewhat prone to races part
        // there is an instruction CMPXCHG8B that would fix this
        // but it's pentium and newer, and I'm targeting i486
        mutex->__owner_tcb_field = &our_tcb->tid;
        // I really want to minimize amount of instructions before the mutex->__owner_tcb_field
        __atomic_thread_fence(__ATOMIC_ACQUIRE);


        mutex->__state = 1;

        // only way to happen is if owner died and we got goto'd from below
        if (is_dead) {
            mutex->__inconsistent = 1;
            syscall(SYSCALL_FUTEX, &mutex->__ownerx, FUTEX_WAKE, 1);

            // we internally make all mutexes robust,
            // but expose just enough to the user application
            //   to make their mutex handling just as broken as they wish :3
            if (mutex->__attr.__robust)
                return EOWNERDEAD;
        }
        return 0;
    }

    // we failed, lock is locked
    // need to check whether the owner is dead or not to return EOWNERDEAD
    // this could theoretically be prone to a race where the thread dying
    // after the mutex is released means an inconsistent mutex
    if (expected.__owner == (unsigned long)us) {
        if (mutex->__attr.__type == PTHREAD_MUTEX_ERRORCHECK)
            return EDEADLK;
        if (mutex->__attr.__type == PTHREAD_MUTEX_RECURSIVE) {
            if (mutex->__state == PTHREAD_MUTEX_RECURSIVE_MAX)
                return EAGAIN;
            mutex->__state ++;
            return 0;
        }
        return EBUSY;
    }

    expected.__ownerx = mutex->__ownerx;
    if (!mutex->__owner_tcb_field || (unsigned long)*mutex->__owner_tcb_field != mutex->__owner) {
        // cheap fix to that race in the single assignment a bit up
        yield();
        if (!mutex->__owner_tcb_field || (unsigned long)*mutex->__owner_tcb_field != mutex->__owner) {
            is_dead = 1;
            goto docas;
        }
    }
    return EBUSY;
}
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (is_klibc) return 0;
    if (mutex == NULL)
        return EINVAL;

    if (mutex->__attr.__robust &&
        (mutex->__inconsistent || mutex->__unrecoverable)) {
        mutex->__unrecoverable = 1;
        if (mutex->__contended)
            syscall(SYSCALL_FUTEX, &mutex->__ownerx, FUTEX_WAKE, ULONG_MAX);

        mutex->__contended = 0;
        mutex->__state = 0;
        return 0;
    }

    pid_t current_pthread = pthread_self()->__tid;
    // theoretically it's undefined behavior for normal (non-robust) to throw EPERM like this
    // I think it makes more sense though + it's undefined anyway so who cares
    if (mutex->__state == 0 ||
        mutex->__owner != (unsigned long)current_pthread)
            return EPERM;

    // robust mutex unlocks are allowed only from the owner
    // so no races possible
    if (!--mutex->__state) {
        unsigned long was_contented = mutex->__contended;
        __atomic_store_n(&mutex->__ownerx, 0, __ATOMIC_RELEASE);
        if (was_contented)
            syscall(SYSCALL_FUTEX, &mutex->__ownerx, FUTEX_WAKE, ULONG_MAX);
    }
    return 0;
}
