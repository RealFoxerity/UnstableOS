#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

#include <stddef.h>
#include "../../libc/src/include/time.h"
#include "../../libc/src/include/signal.h"
#include "../../libc/src/include/sys/limits.h"
#include "kernel_interrupts.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include "fs/fs.h"

#if SIGRTMAX - SIGRTMIN > RTSIG_MAX
#error "Realtime signal ranges larger than realtime signal count!"
#endif

#define PROGRAM_STACK_SIZE (1<<20) // 1MiB please keep a multiple of page size
#if PROGRAM_STACK_SIZE % PAGE_SIZE_NO_PAE != 0
#error "Program stack size is not a multiple of page size!"
#endif

#define PROGRAM_KERNEL_STACK_SIZE (1<<13) // 8KiB

#define ___PROGRAM_STACK_VADDR (0xF0000000) // top, need this as an integer for the #if to work
#define PROGRAM_STACK_VADDR ((void*)___PROGRAM_STACK_VADDR) // top

#define GET_STACK_IDX_FROM_ADDR(vaddr) ((PROGRAM_STACK_VADDR - vaddr)/PROGRAM_STACK_SIZE) // returns the index to the stack bitmap for process
#define GET_STACK_ADDR_FROM_IDX(index) (PROGRAM_STACK_VADDR - i*PROGRAM_STACK_SIZE) // gets the top

#define ___PROGRAM_HEAP_VADDR (0xA0000000) // base
#define PROGRAM_HEAP_VADDR ((void*)___PROGRAM_HEAP_VADDR) // base
#define PROGRAM_HEAP_SIZE (0x40000000) // 1GiB
#define PROGRAM_HEAP_START_SIZE (0x90000) // 0.5MiB, the initially allocated amount

#define PROGRAM_MINIMUM_AVAIL_MEMORY (PROGRAM_HEAP_START_SIZE+PROGRAM_STACK_SIZE+PROGRAM_KERNEL_STACK_SIZE)

#if (___PROGRAM_HEAP_VADDR + PROGRAM_HEAP_SIZE > ___PROGRAM_STACK_VADDR - PTHREAD_THREADS_MAX*PROGRAM_STACK_SIZE)
#error "\
Processes' memory map would have thread stacks and heap overlap!\n\
Consider lowering thread count, increasing stack base address, lowering heap address, and/or decreasing stack and heap sizes"
#endif

enum pstatus_t {
    SCHED_RUNNING,
    SCHED_RUNNABLE,

    SCHED_INTERR_SLEEP,
    SCHED_UNINTERR_SLEEP,
    SCHED_WAITING, // process called wait()

    SCHED_THREAD_CLEANUP, // thread called thread_exit()
} typedef pstatus_t;

#define FD_LIMIT_PROCESS OPEN_MAX

struct sem_t;

#define sa_to_be_handled sa_info_to_be_handled.si_signo
struct thread_t {
    size_t instances; // so that queues don't do UAF when a thread terminates
    size_t tid;
    PAGE_DIRECTORY_TYPE * cr3_state; // if a kernel routine switched address spaces and was then preempted
    mcontext_t context;
    pstatus_t status;

    void * kernel_stack;
    size_t kernel_stack_size; // basically unused since we have a static stack size

    void * stack;
    size_t stack_size;

    // bitmask of signals that the thread ignores
    sigset_t sa_mask;
    // these values are here to make pthread_kill easier to implement in the future

    // thread was chosen to handle a signal; scheduler "calls" signals
    // needed because we can't launch signals inside syscalls

    siginfo_t sa_info_to_be_handled;

    // so that if a queue would've unblocked a thread that was no longer blocked
    // by that queue, it doesn't
    unsigned int magic_queue_value;

    // to avoid deadlocks in syscalls with signals present
    // see CRIT_SEC_START and CRIT_SEC_END
    unsigned long in_critical_section;

    struct thread_t * prev;
    struct thread_t * next;
} typedef thread_t;

struct tty_t; // kernel_tty_io.h
struct session_t {
    struct tty_t * controlling_terminal;
} typedef session_t;

struct rt_siginfo_ll {
    siginfo_t info;
    struct rt_siginfo_ll * next;
};
struct process_t {
    unsigned char ring; // so that drivers and kernel can have their own processes
    struct process_t * parent;
    pid_t pid,
        pgrp, // used for tty interrupts
        session;

    unsigned long uid, gid;
    PAGE_DIRECTORY_TYPE * address_space_paddr;

    char thread_stacks[PTHREAD_THREADS_MAX];
    // a way to keep track of available address ranges, 1 = used
    // PROGRAM_STACK_VADDR - i*PROGRAM_STACK_SIZE

    size_t argc;
    const char ** argv;
    //const char * envp;

    struct sem_t * semaphores[SEM_NSEMS_MAX];

    struct sigaction sa_handlers[NSIG_MAX - 1];

    // this structure only applies for <SIGRTMIN signals
    sigset_t sa_pending;
    siginfo_t sa_pending_info[SIGRTMIN-1];

    int                    sa_rt_queue_count;
    struct rt_siginfo_ll * sa_rt_queue;
    struct rt_siginfo_ll * sa_rt_queue_last;

    inode_t * pwd, * root; // chdir(), chroot()

    file_descriptor_t * fds[FD_LIMIT_PROCESS];

    clock_t user_clicks, system_clicks;
    clock_t dead_user_clicks, dead_system_clicks;

    char is_stopped;
    char do_cleanup;

    long exitcode;
    int postmortem_wstatus;

    thread_t * threads;

    spinlock_t lock;

    struct process_t * prev;
    struct process_t * next;
} typedef process_t;

struct program {
    PAGE_DIRECTORY_TYPE * pd_vaddr;
    void * start;
    void * stack;
    void * heap;
};


// intentionally here, so that process_t and thread_t is already defined for kernel_sched_queues.h
#include "kernel_sched_queues.h"
// these unfortunately have to be here so that process_t and thread_t work
void thread_queue_unblock(thread_queue_t * thread_queue);
void thread_queue_unblock_all(thread_queue_t * thread_queue);
void thread_queue_add(thread_queue_t * thread_queue, process_t * pprocess, thread_t * thread, enum pstatus_t new_status);
struct sem_t {
    unsigned long used;
    unsigned long value;
    struct thread_queue waiting_queue;
} typedef sem_t;


void kernel_idle();
void scheduler_init();
void schedule(mcontext_t * context);
void scheduler_print_process(const process_t * process);
void scheduler_print_processes();


// kernel_sched_sleep_queue.c
void sleep_sched_tick();
ssize_t sys_nanosleep(process_t * pprocess, thread_t * thread, struct timespec requested, struct timespec * elapsed);

void reschedule();

// 0 = couldn't destroy - not safe to destroy a kernel thread
// both assume a locked scheduler (and by extension a critical section)
char kernel_destroy_thread(process_t * parent_process, thread_t * current_thread);
thread_t *  kernel_create_thread(process_t * parent_process, void (* entry_point)(void*), void * arg);

extern process_t * process_list;
extern process_t * zombie_list;

extern process_t * current_process;
extern thread_t * current_thread;
extern process_t * kernel_task;

extern spinlock_t scheduler_lock;

extern pid_t last_pid;
extern pid_t last_tid;


// kernel_signals.c
int sys_kill(pid_t pid, int sig);
int sys_tgkill(pid_t tgid, pid_t tid, int sig);
int sys_sigaction(int sig, struct sigaction * __restrict act, struct sigaction * __restrict oact);
void sys_sigreturn(mcontext_t * ctx);
int sys_sigprocmask(int how, const sigset_t * __restrict set, sigset_t * oset);
int sys_sigsuspend(const sigset_t * set);
int sys_sigqueue(pid_t pid, int signo, union sigval value);

// lock scheduler beforehand or have interrupts disabled
void signal_process(process_t * signaled, siginfo_t * sig);
void signal_thread(process_t * group, thread_t * thread, siginfo_t * sig);
// retries all pending signals
void signal_retry_process(process_t * signaled);

// meant to be called from the syscall, expections, scheduler - dispatches a thread's pending signal
// make sure that the thread is not running, or that it's context replacement
// doesn't break it
// currently assumes the current address space to be the target one, TODO: fix when finally implementing correct memcpy
void signal_dispatch_sa(process_t * group, thread_t * thread);

int signal_process_group(pid_t process_group, siginfo_t * info);

// if defined, allows threads in syscalls not currently in critical sections to be killed
// if not defined, entering syscalls automatically enables a critical section, thus disallowing killing
#define EXIT_AFFECTS_SYSCALLS

// macros to call when the thread cannot be cleaned up safely at that point
// automatically called when doing spinlock_acquire and spinlock_release
// is valid for the duration of a syscall (as the end of a syscall forces counter to 0)
#define CRIT_SEC_START {__atomic_add_fetch(&current_thread->in_critical_section, 1, __ATOMIC_RELAXED);}
#define CRIT_SEC_END {__atomic_sub_fetch(&current_thread->in_critical_section, 1, __ATOMIC_RELAXED);}
#endif