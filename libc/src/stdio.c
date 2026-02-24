#include "include/stdio.h"
#include "include/stdlib.h"
#include "../../src/include/kernel.h"

int open(const char * path, unsigned short flags, unsigned short mode) {
    return syscall(SYSCALL_OPEN, path, flags, mode);
}
int close(int fd) {
    return syscall(SYSCALL_CLOSE, fd);
}
ssize_t write(int fd, const void * buf, size_t count) {
    return syscall(SYSCALL_WRITE, fd, buf, count);
}
ssize_t read (int fd, void * buf, size_t count) {
    return syscall(SYSCALL_READ, fd, buf, count);
}

off_t seek(int fd, off_t offset, int whence) {
    return syscall(SYSCALL_SEEK, fd, offset, whence);
}

int dup(int fd) {
    return syscall(SYSCALL_DUP, fd);
}
int dup2(int oldfd, int newfd) {
    return syscall(SYSCALL_DUP2, oldfd, newfd);
}


int getc(int fd) {
    char ret = EOF;
    read(fd, &ret, 1);
    return ret;
}
int getchar() {return getc(STDIN);}

// really, really, REALLY bad and slow
// this bs will slow down our scanf, a lot
// TODO: fix when finally implementing proper FILE *
char * fgets(char * s, int size, int fd) {
    if (size < 2) return NULL;

    int i;
    for (i = 0; i < size-1; i++) {
        if (read(fd, s+i, 1) != 1) {i--; break;};
        if (s[i] == '\n') break;
        if (s[i] == '\0') return s;
    }
    s[i+1] = '\0';

    return s;
}