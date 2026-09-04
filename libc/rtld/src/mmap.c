#include <UnstableOS/syscalls.h>
#include "basic.h"
#include "mmap.h"

void * mmap(void *addr, unsigned long len, int prot, int flags, int fildes, long long off) {
    return (void*)syscall(SYSCALL_MMAP, addr, len, prot, flags, fildes, &off);
}