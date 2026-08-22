#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <time.h>
#include <sys/types.h>
#include <sched.h>

#define PTHREAD_CANCELED ((void*)0xDEADBEEF)

#define PTHREAD_NULL ((pthread_t)NULL)

#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t){0})
#define PTHREAD_RWLOCK_INITIALIZER ((pthread_rwlock_t){0})
#define PTHREAD_COND_INITIALIZER ((pthread_cond_t){0})

#define PTHREAD_BARRIER_SERIAL_THREAD 1

// pthread_mutex_init_destroy.c
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutex_init(pthread_mutex_t *__restrict mutex, const pthread_mutexattr_t *__restrict attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);

// pthread_mutexattr.c
// this being 0 to allow the mutex initializer being {0}
// default being errorcheck to allow for pthread object implementations throwing EDEADLK
#define PTHREAD_MUTEX_ERRORCHECK    0
#define PTHREAD_MUTEX_NORMAL        1
#define PTHREAD_MUTEX_RECURSIVE     2
#define PTHREAD_MUTEX_DEFAULT       PTHREAD_MUTEX_ERRORCHECK

int pthread_mutexattr_gettype(const pthread_mutexattr_t *__restrict attr, int *__restrict type);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);

// don't set the default (value 0) as PTHREAD_MUTEX_ROBUST,
// a lot of internal pthread machinery doesn't account for it being able to return errors on lock (EOWNERDEAD)
#define PTHREAD_MUTEX_STALLED 0
#define PTHREAD_MUTEX_ROBUST  1

int pthread_mutexattr_getrobust(const pthread_mutexattr_t *__restrict attr, int *__restrict robust);
int pthread_mutexattr_setrobust(pthread_mutexattr_t *attr, int robust);

// pthread_mutex.c
int pthread_mutex_consistent(pthread_mutex_t *mutex);

int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_clocklock(pthread_mutex_t *mutex, clockid_t clock_id, const struct timespec *restrict abstime);
int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *restrict abstime);

// pthread_spin.c
int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);


// pthread_rwlockattr.c
int pthread_rwlockattr_init(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr);

// pthread_rwlock_init_destroy.c
int pthread_rwlock_init(pthread_rwlock_t *restrict rwlock, const pthread_rwlockattr_t *restrict attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);

// pthread_rwlock_rdlocks.c
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_clockrdlock(pthread_rwlock_t *restrict rwlock, clockid_t clock_id, const struct timespec *restrict abstime);
int pthread_rwlock_timedrdlock(pthread_rwlock_t *restrict rwlock, const struct timespec *restrict abstime);

// pthread_rwlock_wrlocks.c
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_clockwrlock(pthread_rwlock_t *restrict rwlock, clockid_t clock_id, const struct timespec *restrict abstime);
int pthread_rwlock_timedwrlock(pthread_rwlock_t *restrict rwlock, const struct timespec *restrict abstime);

// pthread_rwlock_unlock.c
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

// pthread_cond_init_destroy.c
int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);
int pthread_condattr_getclock(const pthread_condattr_t *restrict attr, clockid_t *restrict clock_id);
int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id);
int pthread_cond_init(pthread_cond_t *restrict cond, const pthread_condattr_t *restrict attr);
int pthread_cond_destroy(pthread_cond_t *cond);

// pthread_cond_wait.c
int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);
int pthread_cond_clockwait(pthread_cond_t *restrict cond,
       pthread_mutex_t *restrict mutex, clockid_t clock_id,
       const struct timespec *restrict abstime);
int pthread_cond_timedwait(pthread_cond_t *restrict cond,
       pthread_mutex_t *restrict mutex,
       const struct timespec *restrict abstime);

// pthread_cond_signal.c
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_cond_signal(pthread_cond_t *cond);

// pthread_barrier.c
int pthread_barrierattr_init(pthread_barrierattr_t *attr);
int pthread_barrierattr_destroy(pthread_barrierattr_t *attr);
int pthread_barrier_init(pthread_barrier_t *restrict barrier,
    const pthread_barrierattr_t *restrict attr, unsigned count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);


// pthread_attr.c
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);

int pthread_attr_getguardsize(const pthread_attr_t *__restrict attr, size_t *__restrict guardsize);
int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize);

// pthread_basic.c
pthread_t pthread_self();
int pthread_equal(pthread_t t1, pthread_t t2);
__attribute__((noreturn)) void pthread_exit(void *value_ptr);
int pthread_detach(pthread_t thread);
int pthread_join(pthread_t thread, void **value_ptr);

int pthread_create(pthread_t *__restrict thread,
       const pthread_attr_t *__restrict attr,
       void *(*start_routine)(void*), void *__restrict arg);

// pthread_cancel.c
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_ENABLE  0

#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
int pthread_setcancelstate(int state, int *oldstate);
int pthread_setcanceltype(int type, int *oldtype);
void pthread_testcancel();
int pthread_cancel(pthread_t thread);

// pthread_atfork.c
int pthread_atfork(void (*prepare)(), void (*parent)(), void (*child)());
#endif