#include <stddef.h>
#include <stdarg.h>
#include "include/unistd.h"
#include "include/signal.h"
#include "include/time.h"
#include "include/errno.h"
#include "../../src/include/kernel.h"


int close(int fd) {
    int ret = syscall(SYSCALL_CLOSE, fd);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
ssize_t write(int fd, const void * buf, size_t count) {
    ssize_t ret = syscall(SYSCALL_WRITE, fd, buf, count);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
ssize_t read (int fd, void * buf, size_t count) {
    ssize_t ret = syscall(SYSCALL_READ, fd, buf, count);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

int pipe(int fildes[2]) {
    int ret = syscall(SYSCALL_PIPE, fildes);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

off_t lseek(int fd, off_t offset, int whence) {
    off_t ret = syscall(SYSCALL_SEEK, fd, offset, whence);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

int dup(int fd) {
    int ret = syscall(SYSCALL_DUP, fd);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
int dup2(int oldfd, int newfd) {
    int ret = syscall(SYSCALL_DUP2, oldfd, newfd);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}


int chdir(const char * path) {
    int ret = syscall(SYSCALL_CHDIR, path);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
int chroot(const char * path) {
    int ret = syscall(SYSCALL_CHROOT, path);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

pid_t fork() {
    pid_t ret = syscall(SYSCALL_FORK);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
pid_t spawn(const char * path, char * const* argv, char * const* envp) {
    pid_t ret = syscall(SYSCALL_SPAWN, path, argv, envp);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

pid_t getpid() {
    return syscall(SYSCALL_GETPID);
}
pid_t gettid() {
    return syscall(SYSCALL_GETTID);
}
pid_t getppid() {
    return syscall(SYSCALL_GETPPID);
}

unsigned sleep(unsigned seconds) {
    struct timespec actual = {0};
    struct timespec waited = {.tv_sec = seconds, .tv_nsec = 0};

    long ret = syscall(SYSCALL_NANOSLEEP, &waited, &actual);
    if (ret < 0) return ret;

    return actual.tv_sec;
}

int pause() {
    return sigpause(0);
}

long syscall(unsigned long syscall_number, ...) { // interrupt handler in kernel_syscall.c
    va_list args;
    va_start(args, syscall_number);

    unsigned long arg1 = va_arg(args, unsigned long), arg2 = va_arg(args, unsigned long),
                    arg3 = va_arg(args, unsigned long), arg4 = va_arg(args, unsigned long);

    long out = syscall_number;
    asm volatile (
        "pushl %0;"
        "int $" STR(SYSCALL_INTERR)"\n\t"
        "addl $0x4, %%esp"
        :"+a" (out)
        :"m"(arg4), "D"(arg1), "S"(arg2), "d"(arg3)
    );
    return out;
}