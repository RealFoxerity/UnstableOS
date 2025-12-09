#include "include/stdlib.h"
#include "../../src/include/kernel.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t ___internal_rand_state = 1;

uint32_t rand() {
    ___internal_rand_state = ___internal_rand_state * 1103515245 + 12345;
    return (uint32_t) (___internal_rand_state/(RAND_MAX*2)) % RAND_MAX;
}

void srand(uint32_t seed) {___internal_rand_state = seed;}

long syscall(unsigned long syscall_number, ...) { // interrupt handler in kernel_syscall.c
    va_list args;
    va_start(args, syscall_number);

    unsigned long arg1 = va_arg(args, unsigned long), arg2 = va_arg(args, unsigned long), arg3 = va_arg(args, unsigned long);
    
    long out = syscall_number;
    asm volatile (
        "int $" STR(SYSCALL_INTERR)"\n\t"
        :"+a" (out)
        :"D"(arg1), "S"(arg2), "d"(arg3)
    );
    return out;
}

void exit(long exit_code) {
    syscall(SYSCALL_EXIT, exit_code);
    __builtin_unreachable();
}

void abort() {
    syscall(SYSCALL_ABORT);
    __builtin_unreachable();
}