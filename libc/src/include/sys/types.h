#ifndef _TYPES_H
#define _TYPES_H

typedef long ssize_t;
typedef unsigned long size_t;

typedef unsigned short dev_t;
typedef size_t id_t;
typedef size_t nlink_t;

typedef long long time_t;
typedef unsigned long long clock_t;
typedef short clockid_t;
typedef size_t useconds_t;
typedef ssize_t suseconds_t;

typedef ssize_t pid_t;
typedef ssize_t blksize_t;

typedef id_t gid_t;
typedef id_t uid_t;
// unsigned short would be enough, however stdarg has undefined behavior for lesser types
typedef unsigned int mode_t;

typedef long long off_t;
typedef off_t ino_t;
typedef off_t blkcnt_t;


/* PTHREADS DEFINES */
// don't know why they have to be here, but sure :P

// our layout of the TCB - the pthread object;
// check UnstableOS/tls.h for the kernel provided one
// kernel TCB size limited at 256 bytes!
struct __pthread {
    // kernel objects
    struct __pthread * __self;
    void * __dtv_ptr;
    void * __pcb;
    pid_t __tid;
    unsigned int __thread_slot;

    // our objects
    // volatile as we may be editing from the kernel and userspace at once
    volatile unsigned char __detached;
    volatile unsigned char __cancel_pending;
    volatile unsigned char __cancelable;
    volatile unsigned char __cancelability_type;
    void ** __pthread_keys; // array of size PTHREAD_KEYS_MAX (128)
    void * __cleanup_stack;
    void * __ret;
};
struct {
    int    __detached;
    size_t __guard_size;
} typedef pthread_attr_t;

typedef struct __pthread * pthread_t;

struct {
    unsigned char __type;
    unsigned char __robust;
} typedef pthread_mutexattr_t;

struct {
    pthread_mutexattr_t __attr;
    unsigned char __inconsistent : 1;
    unsigned char __unrecoverable : 1;

    unsigned long __state; // doubles as recursion counter

    union {
        struct {
            unsigned long __owner : 31; // pid is a signed type, so we can squeeze 1 bit for atomic data
            unsigned long __contended : 1; // this has to be last, bitfields on x86 are lsb first
        };
        pid_t __ownerx;
    };

    pid_t * __owner_tcb_field;
} typedef pthread_mutex_t;

// same idea as in the mutex to allow for the (optional) EDEADLK
struct {
    union {
        struct {
            unsigned long __owner : 31;
            unsigned long __locked : 1;
        };
        pid_t __ownerx;
    };
} typedef pthread_spinlock_t;

// only valid rwlock attribute is the pshared attribute,
// since we don't support TSH (Thread Process-Shared Synchronization), it doesn't do anything
// here so that the compiler doesn't complain about initializers
struct {
    int __pshared;
} typedef pthread_rwlockattr_t;

struct {
    pthread_rwlockattr_t __attr;
    unsigned long __val;
    pthread_mutex_t __vlock;
    pthread_mutex_t __wlock;
} typedef pthread_rwlock_t;

struct {
    clockid_t __clockid;
} typedef pthread_condattr_t;

struct {
    pthread_condattr_t __attr;
    pthread_mutex_t __cond_lock; // to guarantee the atomic relock
    unsigned long __magic; // to avoid races between __cond_lock release and futex wait
} typedef pthread_cond_t;

struct {
    int __pshared; // see pthread_rwlockattr_t
} typedef pthread_barrierattr_t;

struct {
    pthread_barrierattr_t __attr;
    unsigned long __requested_count;
    unsigned long __counter;
} typedef pthread_barrier_t;

typedef unsigned int pthread_once_t;

typedef unsigned int pthread_key_t;

#endif