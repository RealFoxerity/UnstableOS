#include "include/unistd.h"
#include "include/fcntl.h"
#include  <UnstableOS/syscalls.h>
#include "include/errno.h"
#include <stdarg.h>

int open(const char * path, unsigned short flags, ...) {
    va_list args;
    va_start(args, flags);

    mode_t mode = va_arg(args, mode_t);
    va_end(args);

    int ret = syscall(SYSCALL_OPENAT, AT_FDCWD, path, flags, mode);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int creat(const char * path, mode_t mode) {
    int ret = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int mkdirat(int fd, const char *path, mode_t mode) {
    return open(path, O_CREAT | O_DIRECTORY, mode);
}

int openat(int fd, const char * path, unsigned short flags, ...) {
    va_list args;
    va_start(args, flags);

    mode_t mode = va_arg(args, mode_t);

    int ret = syscall(SYSCALL_OPENAT, fd, path, flags, mode);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int fcntl(int fildes, int cmd, ...) {
    va_list args;
    va_start(args, cmd);
    long arg = va_arg(args, long);
    va_end(args);

    int ret = syscall(SYSCALL_FCNTL, fildes, cmd, arg);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int unlinkat(int fd, const char *path, int flag) {
    int ret = syscall(SYSCALL_UNLINKAT, fd, path, flag);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}