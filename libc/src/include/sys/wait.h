#ifndef _WAIT_H
#define _WAIT_H
#include "types.h"

#define WEXITSTATUS(wstatus) (wstatus & 0xFF)

int waitpid(pid_t pid, int * status, int options);
pid_t wait(int * wstatus);

#endif