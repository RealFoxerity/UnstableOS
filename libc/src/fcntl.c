#include "include/unistd.h"
#include "include/fcntl.h"
#include "../../src/include/kernel.h"

int open(const char * path, unsigned short flags, unsigned short mode) {
    return syscall(SYSCALL_OPEN, path, flags, mode);
}
int creat(const char * path, unsigned short mode) {
    return open(path, O_CREAT, mode);
}
int openat(int fd, const char * path, unsigned short flags, unsigned short mode) {
    return syscall(SYSCALL_OPEN, fd, path, flags, mode);
}