#include "include/stdio.h"
#include "include/stdlib.h"
#include "../../src/include/kernel.h"

ssize_t write(unsigned int fd, const void * buf, size_t count) {
    return syscall(SYSCALL_WRITE, fd, (unsigned long)buf, count);
}
ssize_t read (unsigned int fd, void * buf, size_t count) {
    return syscall(SYSCALL_READ, fd, (unsigned long)buf, count);
}
