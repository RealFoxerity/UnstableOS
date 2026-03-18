#include <stddef.h>
#include <stdarg.h>
#include "include/unistd.h"
#include "include/time.h"
#include "../../src/include/kernel.h"


int close(int fd) {
    return syscall(SYSCALL_CLOSE, fd);
}
ssize_t write(int fd, const void * buf, size_t count) {
    return syscall(SYSCALL_WRITE, fd, buf, count);
}
ssize_t read (int fd, void * buf, size_t count) {
    return syscall(SYSCALL_READ, fd, buf, count);
}

off_t lseek(int fd, off_t offset, int whence) {
    return syscall(SYSCALL_SEEK, fd, offset, whence);
}

int dup(int fd) {
    return syscall(SYSCALL_DUP, fd);
}
int dup2(int oldfd, int newfd) {
    return syscall(SYSCALL_DUP2, oldfd, newfd);
}


int chdir(const char * path) {
    return syscall(SYSCALL_CHDIR, path);
}
int chroot(const char * path) {
    return syscall(SYSCALL_CHROOT, path);
}

pid_t fork() {
    return syscall(SYSCALL_FORK);
}
pid_t spawn(const char * path, char * const* argv, char * const* envp) {
    return syscall(SYSCALL_SPAWN, path, argv, envp);
}

pid_t getpid() {
    return syscall(SYSCALL_GETPID);
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