#include <stdint.h>
#include "include/stdlib.h"

#define STACK_CHK_GUARD 0xdeadbeef

unsigned long __stack_chk_guard = STACK_CHK_GUARD;

__attribute((noreturn)) void __stack_chk_fail(void) {
    abort();
}