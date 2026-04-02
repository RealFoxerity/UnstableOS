#ifndef _WAIT_H
#define _WAIT_H
#include "types.h"

// these bitmasks are hardcoded in the kernel, so if changing
// fix all instances of wstatus setting
#define WIFEXITED(wstatus)    ((wstatus & 0x00100) != 0)
#define WIFSIGNALED(wstatus)  ((wstatus & 0x00200) != 0)
#define WIFSTOPPED(wstatus)   ((wstatus & 0x00400) != 0)
#define WIFCONTINUED(wstatus) ((wstatus & 0x00800) != 0)

#define WEXITSTATUS(wstatus)  (wstatus  & 0x000FF)
#define WTERMSIG(wstatus)     ((wstatus & 0xFF000) >> 12)

#define WCONTINUED  1
#define WUNTRACED   2
#define WNOHANG     4

pid_t waitpid(pid_t pid, int * wstatus, int options);
pid_t wait(int * wstatus);

#endif