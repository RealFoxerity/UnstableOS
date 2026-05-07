#include "include/unistd.h"
#include "include/fcntl.h"
#include  <UnstableOS/syscalls.h>
#include "include/errno.h"
#include <stdarg.h>

int open(const char * path, unsigned short flags, ...) {
    va_list args;
    va_start(args, flags);

    mode_t mode = va_arg(args, mode_t);

    int ret = syscall(SYSCALL_OPENAT, AT_FDCWD, path, flags, mode);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
int creat(const char * path, mode_t mode) {
    int ret = open(path, O_CREAT, mode);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
int openat(int fd, const char * path, unsigned short flags, ...) {
    va_list args;
    va_start(args, flags);

    mode_t mode = va_arg(args, mode_t);

    int ret = syscall(SYSCALL_OPENAT, fd, path, flags, mode);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}