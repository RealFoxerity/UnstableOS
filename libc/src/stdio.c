#include "include/stdio.h"
#include "include/stdlib.h"
#include "../../src/include/kernel.h"
#include "include/unistd.h"


int getc(int fd) {
    char ret = EOF;
    read(fd, &ret, 1);
    return ret;
}
int getchar() {return getc(STDIN_FILENO);}

int putc(int c, int fd) {
    if (write(fd, &c, 1) <= 0)
        return EOF;
    return c;
}
int putchar(int c) {return putc(c, STDOUT_FILENO);}

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