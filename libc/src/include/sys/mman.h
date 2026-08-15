#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include "types.h" // for mode_t, off_t, size_t

#define PROT_NONE  0
#define PROT_EXEC  1 // not supported on our target, we don't support PAE-NX
#define PROT_READ  2
#define PROT_WRITE 4

#define MAP_ANON    1
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FIXED   2
#define MAP_PRIVATE 4
#define MAP_SHARED  8

#define MAP_FAILED ((void*)0)


void * mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);
int munmap(void *addr, size_t len);
int mprotect(void *addr, size_t len, int prot);
#endif