#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H
#include "kernel_interrupts.h"

#include "mm/kernel_memory.h"
#include "kernel_sched.h"

#define ARG_MAX (PROGRAM_STACK_SIZE/32) // 16KiB of argv

#if ARG_MAX > PROGRAM_STACK_SIZE
#error "ARG_MAX larger than stack size"
#endif

int sys_execve(const char * path, char * const* argv, char * const* envp);
int sys_spawn(const char * path, char * const* argv, char * const* envp);

#include "kernel_sched.h"

char fork_cow_page(void * fault_address); // return 0 = not writable, 1 = writable and remapped
pid_t sys_fork(context_t * ctx);
pid_t sys_wait(int * wstatus);

#endif