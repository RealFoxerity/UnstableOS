#include "include/sys/times.h"
#include "include/unistd.h"
#include "../../src/include/kernel.h"
#include "include/time.h"

clock_t times(struct tms * buffer) {
    clock_t time;

    long ret = syscall(SYSCALL_TIMES, buffer, &time);
    if (ret < 0) return (clock_t)ret;

    return time;
}
