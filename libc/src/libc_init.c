#include "../../src/include/kernel_sched.h"

extern void malloc_prepare(void *heap_struct_start, void *heap_top);
void __libc_init() {
    malloc_prepare(PROGRAM_HEAP_VADDR, PROGRAM_HEAP_VADDR + PROGRAM_HEAP_SIZE);
}