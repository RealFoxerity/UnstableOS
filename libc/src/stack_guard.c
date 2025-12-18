#include "include/stdlib.h"
#include "include/stdio.h"

#define STACK_CHK_VAL 0xdeadbeef

unsigned long __stack_chk_guard = STACK_CHK_VAL;

__attribute__((noreturn)) void __stack_chk_fail(void) {
    printf("Stack smashing detected, aborting!\n");
    abort();
    while(1);
    __builtin_unreachable();
}