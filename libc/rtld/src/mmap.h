#ifndef MMAP_H
#define MMAP_H

#define PROT_NONE  0
#define PROT_EXEC  1 // not supported on our target, we don't support PAE-NX
#define PROT_READ  2
#define PROT_WRITE 4

#define MAP_ANON    1
#define MAP_FIXED   2
#define MAP_PRIVATE 4

void * mmap(void *addr, unsigned long len, int prot, int flags, int fildes, long long off);

#endif