#include "../../src/include/kernel_sched.h"

char ** environ = NULL;
int errno;

extern void malloc_prepare(void *heap_struct_start, void *heap_top);
void __libc_init(int argc, char ** args) {
    malloc_prepare(PROGRAM_HEAP_VADDR, PROGRAM_HEAP_VADDR + PROGRAM_HEAP_SIZE);
    environ = args + argc + 1;
}