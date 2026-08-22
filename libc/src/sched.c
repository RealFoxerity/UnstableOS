#include <sched.h>
#include <unistd.h>
#include <UnstableOS/syscalls.h>

int sched_yield() {
    _syscall(SYSCALL_YIELD);
    return 0;
}