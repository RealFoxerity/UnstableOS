#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H
#include "kernel_interrupts.h"

#include "mm/kernel_memory.h"
#include "kernel_sched.h"

#if ARG_MAX > PROGRAM_STACK_SIZE
#error "ARG_MAX larger than stack size"
#endif

int sys_execve(const char * path, char * const* argv, char * const* envp);
int sys_spawn(const char * path, char * const* argv, char * const* envp);

char fork_cow_page(void * fault_address); // return 0 = not writable, 1 = writable and remapped
pid_t sys_fork(mcontext_t * ctx);
pid_t sys_waitpid(pid_t pid, int * wstatus, int options);

#endif