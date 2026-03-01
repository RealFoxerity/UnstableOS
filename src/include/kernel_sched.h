#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

#include <stddef.h>
#include "kernel_interrupts.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include "fs/fs.h"

#define PROGRAM_STACK_SIZE (1<<16) // 64KiB
#define PROGRAM_KERNEL_STACK_SIZE (1<<13) // 8KiB

#define ___PROGRAM_STACK_VADDR (0xF0000000) // top, need this as an integer for the #if to work
#define PROGRAM_STACK_VADDR ((void*)___PROGRAM_STACK_VADDR) // top

#define PROGRAM_THREADS_MAX 1024 // needed to create a bitmap of all stacks for the given processes' threads for quicker stack allocation
#define PROGRAM_MAX_SEMAPHORES 256

#define GET_STACK_IDX_FROM_ADDR(vaddr) ((PROGRAM_STACK_VADDR - vaddr)/PROGRAM_STACK_SIZE) // returns the index to the stack bitmap for process
#define GET_STACK_ADDR_FROM_IDX(index) (PROGRAM_STACK_VADDR - i*PROGRAM_STACK_SIZE) // gets the top

#define ___PROGRAM_HEAP_VADDR (0xA0000000) // base
#define PROGRAM_HEAP_VADDR ((void*)___PROGRAM_HEAP_VADDR) // base
#define PROGRAM_HEAP_SIZE (0x40000000) // 1GiB
#define PROGRAM_HEAP_START_SIZE (0x90000) // 0.5MiB, the initially allocated amount, call brk/sbrk to increase

#define PROGRAM_MINIMUM_AVAIL_MEMORY (PROGRAM_HEAP_START_SIZE+PROGRAM_STACK_SIZE+PROGRAM_KERNEL_STACK_SIZE)

#if (___PROGRAM_HEAP_VADDR + PROGRAM_HEAP_SIZE > ___PROGRAM_STACK_VADDR - PROGRAM_THREADS_MAX*PROGRAM_STACK_SIZE)
#error "\
Processes' memory map would have thread stacks and heap overlap!\n\
Consider lowering thread count, increasing stack base address, lowering heap address, and/or decreasing stack and heap sizes"
#endif
enum signals {
    SIGINT = 1,
    SIGQUIT,
    SIGTERM,
    SIGALRM,
    SIGSTOP,
    SIGCONT,
    SIGKILL,
    SIGCHLD,
    SIGTTOU, // background process group tried to write to a controlling terminal (with the option TOSTOP), default action is to pause the process
    SIGTTIN, // background process group tried to read from a controlling terminal
    SIGHUP, // controlling terminal was closed by the other side

    __sig_last
};

#define GET_SIG_MASK(signal) (1<<signal)

#define MASK_SIGINT GET_SIG_MASK(SIGINT)
#define MASK_SIGQUIT GET_SIG_MASK(SIGQUIT)
#define MASK_SIGTERM GET_SIG_MASK(SIGTERM)
#define MASK_SIGALRM GET_SIG_MASK(SIGALRM)
#define MASK_SIGSTOP GET_SIG_MASK(SIGSTOP)
#define MASK_SIGCONT GET_SIG_MASK(SIGCONT)
#define MASK_SIGKILL GET_SIG_MASK(SIGKILL)
#define MASK_SIGCHLD GET_SIG_MASK(SIGCHLD)
#define MASK_SIGTTOU GET_SIG_MASK(SIGTTOU)
#define MASK_SIGTTIN GET_SIG_MASK(SIGTTIN)
#define MASK_SIGHUP GET_SIG_MASK(SIGHUP)

enum pstatus_t {
    SCHED_RUNNING,
    SCHED_RUNNABLE,
    
    SCHED_STOPPED, // SIGTTOU, STGTTIN, SIGSTOP, resumes by SIGCONT

    SCHED_INTERR_SLEEP,
    SCHED_UNINTERR_SLEEP,
    SCHED_WAITING, // process called wait()

    SCHED_THREAD_CLEANUP, // thread called thread_exit()
    SCHED_CLEANUP, // process called exit() or otherwise crashed
} typedef pstatus_t;

#pragma clang diagnostic ignored "-Wc99-designator"
static const char after_signal_states[__sig_last] = {
    [SIGINT] = SCHED_RUNNABLE,
    [SIGQUIT] = SCHED_CLEANUP,
    [SIGTERM] = SCHED_CLEANUP,
    [SIGALRM] = SCHED_RUNNABLE,
    [SIGSTOP] = SCHED_STOPPED,
    [SIGCONT] = SCHED_RUNNABLE,
    [SIGKILL] = SCHED_CLEANUP,
    [SIGCHLD] = SCHED_RUNNABLE,
    [SIGTTOU] = SCHED_STOPPED,
    [SIGTTIN] = SCHED_STOPPED,
    [SIGHUP] = SCHED_CLEANUP
};

#define FD_LIMIT_PROCESS 128

struct context_t {
    unsigned long edi, esi;
    void * ebp, *esp;
    unsigned long ebx, edx, ecx, eax;

    struct interr_frame iret_frame;

}  __attribute__((packed)) typedef context_t; 



struct process_t;
struct thread_t;


struct __thread_queue_inner {
    struct process_t * parent_process;
    struct thread_t * thread;
    struct __thread_queue_inner * prev;
    struct __thread_queue_inner * next;
};
struct thread_queue {
    struct __thread_queue_inner queue;
    spinlock_t queue_lock;
} typedef thread_queue_t;

struct sem_t {
    unsigned char used;
    unsigned long value;
    struct thread_queue waiting_queue;
} typedef sem_t;


struct thread_t {
    size_t tid;
    PAGE_DIRECTORY_TYPE * cr3_state; // if a kernel routine switched address spaces and was then preempted
    context_t context;
    pstatus_t status;

    void * kernel_stack;
    size_t kernel_stack_size; // basically unused since we have a static stack size

    void * stack;
    size_t stack_size;

    struct thread_t * prev;
    struct thread_t * next;
} typedef thread_t;

typedef size_t pid_t;

struct tty_t; // kernel_tty_io.h
struct session_t {
    struct tty_t * controlling_terminal;
} typedef session_t;

struct process_t {
    unsigned char ring; // so that drivers and kernel can have their own processes
    pid_t pid, ppid, 
        pgrp, // used for tty interrupts
        session, ses_leader;

    unsigned long uid, gid;
    PAGE_DIRECTORY_TYPE * address_space_vaddr;
    
    char thread_stacks[PROGRAM_THREADS_MAX]; 
    // a way to keep track of available address ranges, 1 = used
    // PROGRAM_STACK_VADDR - i*PROGRAM_STACK_SIZE

    size_t argc;
    const char ** argv;
    //const char * envp;

    long exitcode;

    sem_t semaphores[PROGRAM_MAX_SEMAPHORES];
    unsigned short signal;

    inode_t * pwd, * root; // chdir(), chroot()

    file_descriptor_t * fds[FD_LIMIT_PROCESS];


    char is_stopped;
    thread_t * threads;
    
    struct process_t * prev;
    struct process_t * next;
} typedef process_t;

struct program {
    PAGE_DIRECTORY_TYPE * pd_vaddr;
    void * start;
    void * stack;
    void * heap;
};

void kernel_idle();
void scheduler_init();
void schedule(context_t * context);
void scheduler_print_process(const process_t * process);
void scheduler_print_processes();


void kill();

void reschedule();

void kernel_destroy_thread(process_t * parent_process, thread_t * current_thread);
thread_t *  kernel_create_thread(process_t * parent_process, void (* entry_point)(void*), void * arg);
extern process_t * process_list;
extern process_t * zombie_list;

extern process_t * current_process;
extern thread_t * current_thread;
extern process_t * kernel_task;

extern spinlock_t scheduler_lock;

extern pid_t last_pid;
extern pid_t last_tid;

void thread_queue_unblock(thread_queue_t * thread_queue);
void thread_queue_add(thread_queue_t * thread_queue, process_t * pprocess, thread_t * thread, enum pstatus_t new_status);
void signal_process_group(pid_t process_group, unsigned short signal);
#endif