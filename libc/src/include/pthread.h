#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <time.h>
#include <sys/types.h>
#define PTHREAD_NULL ((pthread_t)NULL)

#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t){0})

// pthread_mutex_init_destroy.c
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutex_init(pthread_mutex_t *__restrict mutex, const pthread_mutexattr_t *__restrict attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);

// pthread_mutexattr.c
#define PTHREAD_MUTEX_NORMAL        0
#define PTHREAD_MUTEX_ERRORCHECK    1
#define PTHREAD_MUTEX_RECURSIVE     2
#define PTHREAD_MUTEX_DEFAULT       PTHREAD_MUTEX_NORMAL

int pthread_mutexattr_gettype(const pthread_mutexattr_t *__restrict attr, int *__restrict type);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);

#define PTHREAD_MUTEX_STALLED 0
#define PTHREAD_MUTEX_ROBUST  1

int pthread_mutexattr_getrobust(const pthread_mutexattr_t *__restrict attr, int *__restrict robust);
int pthread_mutexattr_setrobust(pthread_mutexattr_t *attr, int robust);

// pthread_mutex.c
int pthread_mutex_consistent(pthread_mutex_t *mutex);

int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

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