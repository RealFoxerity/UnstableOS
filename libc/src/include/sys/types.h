#ifndef _TYPES_H
#define _TYPES_H

typedef long ssize_t;
typedef unsigned long size_t;

typedef unsigned short dev_t;
typedef size_t id_t;
typedef size_t nlink_t;

typedef long long time_t;
typedef unsigned long long clock_t;
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

//typedef pid_t pthread_t; // thread id

// our layout of the TCB - the pthread object;
// check UnstableOS/tls.h for the kernel provided one
// kernel TCB size limited at 256 bytes!
struct __pthread {
    // kernel objects
    struct __pthread * __self;
    void * __dtv_ptr;
    void * __pcb;
    pid_t __tid;;
    unsigned int __thread_slot;

    // our objects
    // volatile as we may be editing from the kernel and userspace at once
    volatile unsigned char __detached;
    volatile unsigned char __cancel_pending;
    volatile unsigned char __cancelable;
    volatile unsigned char __cancelability_type;
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
#endif