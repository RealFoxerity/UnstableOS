#include "include/unistd.h"
#include "include/fcntl.h"
#include "../../src/include/kernel.h"
#include "include/errno.h"

int open(const char * path, unsigned short flags, unsigned short mode) {
    int ret = syscall(SYSCALL_OPEN, path, flags, mode);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
int creat(const char * path, unsigned short mode) {
    int ret = open(path, O_CREAT, mode);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
int openat(int fd, const char * path, unsigned short flags, unsigned short mode) {
    int ret = syscall(SYSCALL_OPEN, fd, path, flags, mode);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}