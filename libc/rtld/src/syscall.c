#include <stdarg.h>
#include <UnstableOS/syscalls.h>
#include "basic.h"

static long __vsyscall(unsigned long syscall_number, va_list args) { // interrupt handler in kernel_syscall.c

    unsigned long arg1 = va_arg(args, unsigned long), arg2 = va_arg(args, unsigned long),
                    arg3 = va_arg(args, unsigned long), arg4 = va_arg(args, unsigned long),
                        // basically just used by futex and mmap
                      arg5 = va_arg(args, unsigned long), arg6 = va_arg(args, unsigned long);
    va_end(args);

    long out = (long)syscall_number;
    asm volatile (
        "pushl %1;"
        "pushl %2;"
        "pushl %3;"
        "int $" STR(SYSCALL_INTERR)"\n\t"
        "addl $0xC, %%esp"
        :"+a" (out)
        :"m"(arg6), "m"(arg5), "m"(arg4), "D"(arg1), "S"(arg2), "d"(arg3)
    );
    return out;
}

long syscall(unsigned long syscall_number, ...) {
    va_list args;
    va_start(args, syscall_number);
    long ret = __vsyscall(syscall_number, args);
    va_end(args);
    return ret;
}