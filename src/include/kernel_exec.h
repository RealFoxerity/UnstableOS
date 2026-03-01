#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H
#include "kernel_interrupts.h"

int sys_exec(const char * path);
int sys_spawn(const char * path);

#include "kernel_sched.h"

pid_t sys_fork(context_t * ctx);
pid_t sys_wait(int * wstatus);

#endif