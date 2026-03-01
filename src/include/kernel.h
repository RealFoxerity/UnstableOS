#ifndef KERNEL_H
#define KERNEL_H
#include <stdint.h>
#include "../../libc/src/include/stdio.h"
#include "devs.h"

#define KERNEL_TIMER_RESOLUTION_MSEC 40

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

#define PATH_MAX 4096

#define SYSCALL_INTERR 0xF0 // if changing, change crt0.s

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

enum syscalls {
    SYSCALL_EXIT = 0, // if changing, change crt0.s, exit(long exitcode)
    SYSCALL_ABORT = 1,
    SYSCALL_OPEN,
    SYSCALL_CLOSE,

    SYSCALL_DUP,
    SYSCALL_DUP2,

    SYSCALL_MKDIR,
    //SYSCALL_CREATE, handled by OPEN
    SYSCALL_UNLINK,

    SYSCALL_READ,
    SYSCALL_WRITE,
    SYSCALL_SEEK,

    SYSCALL_STAT,
    SYSCALL_MOUNT,

    SYSCALL_BRK, // sets the end of the data segment to a specified pointer
    SYSCALL_SBRK, // increments the data segment of the current running process, return previous end

    SYSCALL_EXEC,
    SYSCALL_FORK,
    SYSCALL_SPAWN, // spawn a new process (fork() + exec())

    SYSCALL_GETPID,
    //SYSCALL_SETSID,
    SYSCALL_GETPGID, // getpgid(pid_t target_pid)
    SYSCALL_SETPGID, // setpgid(pid_t target_pid, pid_t target_pgid)
    SYSCALL_KILL,
    SYSCALL_WAIT, // waits on any child to exit(), pid_t wait()

    SYSCALL_CREATE_THREAD, // create_thread(void (* entry_point)(void*), void * args)
    SYSCALL_EXIT_THREAD, // like exit() but for threads, no exitcode, has to be done via userspace (see libc/src/threads.c)

    SYSCALL_YIELD,

    //SYSCALL_IOCTL,
    SYSCALL_TCGETPGRP,
    SYSCALL_TCSETPGRP,

    SYSCALL_SEM_INIT, // semaphore_t sem_init(int initial val)
    SYSCALL_SEM_POST, // sem_post(int semaphore id)
    SYSCALL_SEM_WAIT, // sem_wait(int semaphore id)
    SYSCALL_SEM_DESTROY,


    SYSCALL_INTERR_RING2_PANIC = 0xFF,
};

extern unsigned long _kernel_base, _kernel_top, _kernel_stack_top, boot_mem_top;
extern size_t system_time_sec, uptime_clicks;

void __attribute__((format(printf, 1, 2))) kprintf(const char *format, ...);

void __attribute__((noreturn)) panic(char * reason);

void kernel_reset_system(); // kernel_ps2.c
#endif