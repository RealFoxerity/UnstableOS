#ifndef KERNEL_H
#define KERNEL_H
#include <stdint.h>
#include "../../libc/src/include/stdio.h"
#include "devs.h"

#define KERNEL_TIMER_RESOLUTION_MSEC 4
#define RTC_TIMER_RESOLUTION_HZ 1024
#define RTC_TIME_RESOLUTION_USEC (1000000 / RTC_TIMER_RESOLUTION_HZ)

#define KERNEL_VERSION "UnstableOS v0.01"

#define __STR_INNER(x) #x
#define STR(x) __STR_INNER(x)

#define kabort() {asm volatile ("mov %0, %%eax; int $" STR(SYSCALL_INTERR) :: "r"(SYSCALL_ABORT)); asm volatile ("1:; hlt; jmp 1b");} // hlt loop if interrupts were disabled and thus syscall wouldn't fire

// most probably it doesn't matter anyway, so just kill the kernel at that point...
#define kassert(cond) {\
    if (!(cond)) {\
        char errmsg[128];\
        sprintf(errmsg, "Kernel assertion `"#cond"` failed in %s()! [" __FILE__ ":" STR(__LINE__) "]\n", __func__);\
        panic(errmsg);\
    }\
}

#define UNLINK_DOUBLE_LINKED_LIST(item, list) {     \
    if (item->next != NULL)                         \
        item->next->prev = item->prev;              \
    else                                            \
        list->prev = item->prev;                    \
    if (item != list)                               \
        item->prev->next = item->next;              \
    else                                            \
        list = item->next;                          \
}

#define APPEND_DOUBLE_LINKED_LIST(item, list) {     \
    item->next = NULL;                              \
    if (list == NULL) {                             \
        list = item;                                \
    } else {                                        \
        item->prev = list->prev;                    \
        list->prev->next = item;                    \
    }                                               \
    list->prev = item;                              \
}

#define SYSCALL_INTERR 0xF0 // if changing, change crt0.s

enum syscalls {
    SYSCALL_EXIT = 0, // if changing, change crt0.s, exit(long exitcode)
    SYSCALL_ABORT = 1,
    SYSCALL_OPEN,
    SYSCALL_OPENAT,
    SYSCALL_CLOSE,

    SYSCALL_DUP,
    SYSCALL_DUP2,

    SYSCALL_MKDIR,
    //SYSCALL_CREATE, handled by OPEN
    SYSCALL_UNLINK,

    SYSCALL_READ,
    SYSCALL_WRITE,
    SYSCALL_SEEK,

    SYSCALL_READDIR, // theoretically could be implemented in read()

    SYSCALL_PIPE,

    SYSCALL_CHDIR,
    SYSCALL_CHROOT,

    SYSCALL_FSTAT,
    SYSCALL_FSTATAT,

    SYSCALL_MOUNT,

    SYSCALL_EXEC,
    SYSCALL_FORK,
    SYSCALL_SPAWN, // spawn a new process (fork() + exec())

    SYSCALL_GETPID,
    SYSCALL_GETPPID,
    SYSCALL_GETTID,
    //SYSCALL_SETSID,
    SYSCALL_GETPGID, // getpgid(pid_t target_pid)
    SYSCALL_SETPGID, // setpgid(pid_t target_pid, pid_t target_pgid)

    SYSCALL_KILL,
    SYSCALL_TGKILL,

    SYSCALL_SIGACTION,
    SYSCALL_SIGRETURN,
    SYSCALL_SIGPROCMASK,
    SYSCALL_SIGPENDING,
    SYSCALL_SIGSUSPEND,
    SYSCALL_SIGQUEUE,

    SYSCALL_WAITPID, // the same as the waitpid() function

    SYSCALL_CREATE_THREAD, // create_thread(void (* entry_point)(void*), void * args)
    SYSCALL_EXIT_THREAD, // like exit() but for threads, no exitcode, has to be done via userspace (see libc/src/threads.c)

    SYSCALL_YIELD,

    SYSCALL_NANOSLEEP,
    SYSCALL_TIME,

    // different than the function since we can't easily return 64 bits: struct tms * buffer, clock_t * elapsed
    // we could do 32 bits, but then the 2038 problem comes to bite us
    SYSCALL_TIMES,

    //SYSCALL_IOCTL,
    SYSCALL_TCGETPGRP,
    SYSCALL_TCSETPGRP,

    SYSCALL_SEM_INIT, // semaphore_t sem_init(int initial val)
    SYSCALL_SEM_POST, // sem_post(int semaphore id)
    SYSCALL_SEM_WAIT, // sem_wait(int semaphore id)
    SYSCALL_SEM_DESTROY,
};

extern unsigned long _kernel_base, _kernel_top, _kernel_stack_top, boot_mem_top;
extern time_t system_time_sec, uptime_clicks;

void __attribute__((format(printf, 1, 2))) kprintf(const char *format, ...);

void __attribute__((noreturn)) panic(char * reason);

void kernel_reset_system(); // kernel_ps2.c



/****** feature macros ******/
#define HEAP_POISONING // fills freed chunks with 0x41 and allocated with 0x62

/*
numerous reasons to reschedule:
avoid kernel starvation by a syscall spamming thread
make signals to a *process* forced

since we are already in ring 0, the penalty for calling reschedule is almost zero

however, in a thread that uses syscall very frequently (e.g. writing to a framebuffer)
this would make it extremely slow
if not selected, reschedule only happens on cleanup and non-running thread states (sleep)
*/
//#define SYSCALLS_RESCHEDULE

// most devices start at sc2 and need lookup tables to convert to sc1
// some devices allow to directly set them as sc1, meaning we can skip the conversion
#define PS2_TRY_TO_NEGOTIATE_SC1
#define PS2_MOUSE_PACKET_SPEED 40 // per second; can be 10, 20, 40, 80, 100, 200
//#define PS2_MOUSE_LINUX_COMPAT // makes the psaux device work as it does on linux - 3 bytes; no scroll wheel/5 buttons

// assuming the monitor and gpu is from at least 1994, it should support DDC/EDID
// in cases it doesn't, we can either give up, or assume it's a virtual monitor/gpu that doesn't implement DDC/EDID
// e.g. QEMU qxl vga device
#define VBE_EDID_ASSUME_VIRTUAL_ON_FAILURE

#endif