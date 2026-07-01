#include <stddef.h>
#include <unistd.h>
#include <UnstableOS/syscalls.h>

char ** environ = NULL;

#define START_HEAP_SIZE 0x8000000 // 128MiB

extern void malloc_prepare(void *heap_struct_start, void *heap_top);
extern void __stdio_init();
void __libc_init(int argc, char ** args) {
    void * heap_start = sbrk(START_HEAP_SIZE);

    malloc_prepare(heap_start, heap_start + START_HEAP_SIZE);
    environ = args + argc + 1;

    __stdio_init();
}