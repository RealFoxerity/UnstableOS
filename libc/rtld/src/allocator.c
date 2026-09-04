#include <UnstableOS/syscalls.h>
#include "basic.h"
#include "mmap.h"
#include <limits.h>
#include <sys/types.h>

#define HEAP_SIZE (8 * PAGE_SIZE)
static void * heap_start = (void*)0x7F7F6000 - HEAP_SIZE;

void init_heap() {
    heap_start = mmap(
        heap_start, HEAP_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_ANON | MAP_PRIVATE | MAP_FIXED,
        -1, 0);
    if (heap_start != (void*)0x7F7F6000 - HEAP_SIZE) {
        dbg_print("rtld: heap mmap failed\n");
        abort();
    }
}

void * alloc(unsigned long n) {
    static void * heap_top = (void*)0x7F7F6000 - HEAP_SIZE;
    if (n > (void*)0x7F7F6000 - heap_top) {
        dbg_print("rtld: alloc failed - end of heap\n");
        abort();
    }
    heap_top += n;
    return heap_top - n;
}