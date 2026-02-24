#include "include/stdio.h"
#include "include/stdlib.h"
#include "../../src/include/kernel.h"

ssize_t write(int fd, const void * buf, size_t count) {
    return syscall(SYSCALL_WRITE, fd, (unsigned long)buf, count);
}
ssize_t read (int fd, void * buf, size_t count) {
    return syscall(SYSCALL_READ, fd, (unsigned long)buf, count);
}

int dup(int fd) {
    return syscall(SYSCALL_DUP, fd);
}
int dup2(int oldfd, int newfd) {
    return syscall(SYSCALL_DUP2, oldfd, newfd);
}