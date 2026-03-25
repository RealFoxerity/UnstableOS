#include "include/sys/stat.h"
#include "include/fcntl.h"
#include "include/unistd.h"
#include "include/errno.h"
#include "../../src/include/kernel.h"

int stat(const char * __restrict path, struct stat * __restrict buf) {
    int ret = syscall(SYSCALL_FSTATAT, AT_FDCWD, path, buf, 0);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

int fstat(int fd, struct stat * buf) {
    int ret = syscall(SYSCALL_FSTAT, fd, buf);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

int fstatat(int fd, const char * __restrict path, struct stat * __restrict buf, int flags) {
    int ret = syscall(SYSCALL_FSTATAT, fd, path, buf, flags);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}