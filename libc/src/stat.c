#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <UnstableOS/syscalls.h>

int stat(const char * __restrict path, struct stat * __restrict buf) {
    int ret = syscall(SYSCALL_FSTATAT, AT_FDCWD, path, buf, 0);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int fstat(int fd, struct stat * buf) {
    int ret = syscall(SYSCALL_FSTAT, fd, buf);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int fstatat(int fd, const char * __restrict path, struct stat * __restrict buf, int flags) {
    int ret = syscall(SYSCALL_FSTATAT, fd, path, buf, flags);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

mode_t umask(mode_t mask) {
    int ret = syscall(SYSCALL_UMASK, mask);
    if (ret < 0) { // should never happen, but in case we want portability or smth
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int mkdir(const char *path, mode_t mode) {
    return mkdirat(AT_FDCWD, path, mode);
}
int mkdirat(int fd, const char *path, mode_t mode) {
    int ret = open(path, O_CREAT | O_DIRECTORY, mode);
    if (ret < 0)
        return ret;
    close(ret);
    return 0;
}

int mkfifoat(int fd, const char *path, mode_t mode) {
    int ret = syscall(SYSCALL_MKFIFOAT, fd, path, mode);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int mkfifo(const char *path, mode_t mode) {
    return mkfifoat(AT_FDCWD, path, mode);
}

static int __mknodat(int fd, const char *path, mode_t mode, dev_t dev) {
    int ret = syscall(SYSCALL_MKNODAT, fd, path, mode, dev);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int mknod(const char *path, mode_t mode, dev_t dev) {
    if (S_ISREG(mode))
        return creat(path, mode);
    if (S_ISDIR(mode))
        return mkdir(path, mode);
    if (S_ISFIFO(mode))
        return mkfifo(path, mode);
    return __mknodat(AT_FDCWD, path, mode, dev);
}
int mknodat(int fd, const char *path, mode_t mode, dev_t dev) {
    if (S_ISREG(mode))
        return openat(fd, path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (S_ISDIR(mode))
        return mkdirat(fd, path, mode);
    if (S_ISFIFO(mode))
        return mkfifoat(fd, path, mode);
    return __mknodat(fd, path, mode, dev);
}