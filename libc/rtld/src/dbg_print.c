#include "basic.h"
#include <UnstableOS/syscalls.h>

#include "string.h"

void dbg_print(const char * s) {
    syscall(SYSCALL_WRITE, 2, s, strlen(s));
}
