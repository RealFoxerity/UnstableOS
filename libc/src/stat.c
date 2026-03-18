#include "include/sys/stat.h"
#include "include/fcntl.h"
#include "include/unistd.h"
#include "../../src/include/kernel.h"

int stat(const char * __restrict path, struct stat * __restrict buf) {
    return syscall(SYSCALL_FSTATAT, AT_FDCWD, path, buf, 0);
}

int fstat(int fd, struct stat * buf) {
    return syscall(SYSCALL_FSTAT, fd, buf);
}

int fstatat(int fd, const char * __restrict path, struct stat * __restrict buf, int flags) {
    return syscall(SYSCALL_FSTATAT, fd, path, buf, flags);
}