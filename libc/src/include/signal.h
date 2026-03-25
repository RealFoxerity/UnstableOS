#ifndef _SIGNAL_H
#define _SIGNAL_H


#include "sys/types.h"
#include <stdint.h>

#define GET_SIG_MASK(signal) (1<<(signal-1))

typedef uint64_t sigset_t;
#define NSIG_MAX 64 // signals that can be saved in the sigset_t
#define NSIG NSIG_MAX // amount of signals the system supports, legacy
#define SIGMAX NSIG // same as nsig

#define SIGRTMIN 32
#define SIGRTMAX 64

typedef void(* __sighandler_t)(int);

#define SIG_ERR  ((__sighandler_t)-1) // return value of signal() on error (NULL on success)

#define SIG_DFL  ((__sighandler_t)0) // leave as NULL!
#define SIG_IGN  ((__sighandler_t)1)
#define SIG_HOLD  ((__sighandler_t)2) // for sigset, but sigaction and signal will accept it

union sigval {
    int sival_int;
    void * sival_ptr;
};

struct {
    int si_signo;
    int si_code;

    // int si_errno;

    union {
        struct {
            pid_t si_pid;           // for SIGCHLD
            uid_t si_uid;           // for SIGCHLD
            int si_status;          // for SIGCHLD
        };
        void * si_addr;         // for SIGILL and SIGSEGV
    };
    union sigval si_value;
} typedef siginfo_t;

struct {
    void * ss_sp;
    size_t ss_size;
    int ss_flags;
} typedef stack_t;

// stack_t ss_flags
#define SS_ONSTACK 1
#define SS_DISABLE 0

struct interr_frame {
    void * ip; // instruction pointer
    unsigned long cs; // code segment
    unsigned long flags; // check flags, e.g. LT IZ ...
    void * sp; // stack pointer
    unsigned long ss; // stack segment
} __attribute__((packed));

struct {
    unsigned long edi, esi;
    void * ebp, *esp;
    unsigned long ebx, edx, ecx, eax;

    struct interr_frame iret_frame;

}  __attribute__((packed)) typedef mcontext_t;

struct ucontext_t {
    struct ucontext_t * uc_link;
    sigset_t uc_sigmask;
    stack_t uc_stack;
    mcontext_t uc_mcontext;
} typedef ucontext_t;

struct sigaction {
    sigset_t sa_mask;
    int sa_flags;

    union {
        void (*sa_sigaction)(int, siginfo_t *, ucontext_t *);
        __sighandler_t sa_handler;
    };

    void (*__restorer); // trampoline code from userspace, is supposed to call sigreturn
};

// sa_flags
#define SA_NOCLDSTOP    0x1
// #define SA_ONSTACK // not supported
#define SA_RESETHAND    0x4
//#define SA_RESTART // not supported
#define SA_SIGINFO      0x10 // not checked by the kernel; kernel just pushes args as if set
#define SA_NOCLDWAIT    0x20
#define SA_NODEFER      0x40

// sigprocmask "how" argument
#define SIG_BLOCK 0
#define SIG_SETMASK 1
#define SIG_UNBLOCK 2

// named signals from sigset_t
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5 // not called by the kernel - traps not implemented
#define SIGABRT   6
#define SIGIOT    SIGABRT
#define SIGBUS    7
#define SIGFPE    8 // not called by the kernel - FPU not implemented
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13 // not called by the kernel - pipes not implemented
#define SIGALRM   14 // not called by the kernel - alarms not implemented
#define SIGTERM   15
#define SIGSTKFLT 16 // stack fault on a coprocessor
#define SIGCHLD   17
#define SIGCLD    SIGCHLD
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23 // not called by the kernel - urgent data (and sockets) not implemented
#define SIGXCPU   24 // cpu time limit reached
#define SIGXFSZ   25 // file size limit reached
#define SIGVTALRM 26 // virtual alarm clock
#define SIGPROF   27 // profiling timer
#define SIGWINCH  28 // not called by the kernel - window change notifications not implemented
#define SIGIO     29 // not called by the kernel - async io not implemented
#define SIGPOLL   SIGIO
#define SIGPWR    30 // System V power fault
#define SIGSYS    31 // not called by the kernel - SVr4 features and seccomp not implemented - instead always returning -ENOSYS


// signals si_code for siginfo_t
// for SIGILL
/* we don't yet support any of these - detection not implemented
#define ILL_ILLOPC 1
#define ILL_ILLOPN 2
#define ILL_ILLADR 3
#define ILL_ILLTRP 4
#define ILL_PRVOPC 5
#define ILL_PRVREG 6
#define ILL_COPROC 7
#define ILL_BADSTK 8
*/

// for SIGFPE
/* we don't yet support any of these - no FPU is implemented
#define FPE_INTDIV 1
#define FPE_INTOVF 2
#define FPE_FLTDIV 3
#define FPE_FLTOVF 4
#define FPE_FLTUND 5
#define FPE_FLTRES 6
#define FPE_FLTINV 7
#define FPE_FLTSUB 8
*/

// for SIGSEGV
#define SEGV_MAPERR 1
#define SEGV_ACCERR 2

// for SIGBUS
/* we don't yet support any of these - detection not implemented
#define BUS_ADRALN 1
#define BUS_ADRERR 2
#define BUS_OBJERR 3
*/

// for SIGTRAP
/* we don't yet support any of these - traps aren't implemented
#define TRAP_BRKPT 1
#define TRAP_TRACE 2
*/

// for SIGCHLD
#define CLD_EXITED    1
#define CLD_KILLED    2
//#define CLD_DUMPED    3 // not supported - we don't yet support core dumping
//#define CLD_TRAPPED   4 // not supported - we don't yet support traps
#define CLD_STOPPED   5
#define CLD_CONTINUED 6

// general codes
#define SI_USER    0x10
#define SI_QUEUE   0x20
//#define SI_TIMER   0x40 // not supported - timers not implemented
//#define SI_ASYNCIO 0x80 // not supported - AIO not implemented
//#define SI_MESGQ   0x100 // not supported - message IPC not implemented


int kill(pid_t pid, int sig);
int killpg(pid_t pgrp, int sig);
int tgkill(pid_t tgid, pid_t tid, int sig); // equivalent to pthread_kill

int sigaction(int sig, const struct sigaction *__restrict act, struct sigaction *__restrict oact);
int sigprocmask(int how, const sigset_t * __restrict set, sigset_t * __restrict oset);
int sigsuspend(const sigset_t *sigmask);
int pthread_sigmask(int how, const sigset_t * __restrict set, sigset_t * __restrict oset);

void (*signal(int sig, void (*func)(int)))(int); // internally calls sigaction


int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigismember(const sigset_t *set, int signo);
int sigpending(sigset_t *set);

int sighold(int sig);
int sigignore(int sig);
int sigpause(int sig);
int sigrelse(int sig);
void (*sigset(int sig, void (*disp)(int)))(int); // internally calls signal
int sigqueue(pid_t pid, int signo, union sigval value);

/*
missing functions:
void   psiginfo(const siginfo_t *, const char *);
void   psignal(int, const char *);
int    pthread_kill(pthread_t, int); though implemented through tgkill
int    sigaltstack(const stack_t *restrict, stack_t *restrict);

int siginterrupt(int sig, int flag); because we don't support SA_RESTART

int sigtimedwait(const sigset_t *restrict set,
       siginfo_t *restrict info,
       const struct timespec *restrict timeout);
int sigwaitinfo(const sigset_t *restrict set,
       siginfo_t *restrict info);


void siglongjmp(sigjmp_buf env, int val);
int sigsetjmp(sigjmp_buf env, int savemask);

char *strsignal(int signum);
*/
#endif