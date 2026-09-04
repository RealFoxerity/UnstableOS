#include "basic.h"
#include "files.h"
#include <UnstableOS/syscalls.h>

int openat(int fd, const char * path, unsigned short flags) {
    return syscall(SYSCALL_OPENAT, fd, path, flags, 0);
}

int close(int fd) {
    return syscall(SYSCALL_CLOSE, fd);
}


long pread(int fd, void * buf, unsigned long count, unsigned long long offset) {
    return syscall(SYSCALL_PREAD, fd, buf, count, offset);
}