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

#include "include/string.h"

#include "errno_msgs.h"

#define ERRNO_STRING_LEN 64

void perror(const char * s) {
    if (s != NULL) {
        fprintf(STDERR_FILENO, "%s: ", s);
    }
    fprintf(STDERR_FILENO, "%s\n", strerror(errno));
}

char *strerror(int errnum) {
    static char errno_string[ERRNO_STRING_LEN];
    strerror_r(errnum, errno_string, ERRNO_STRING_LEN);
    return errno_string;
}

int strerror_r(int errnum, char *strerrbuf, size_t buflen) {
    if (strerrbuf == NULL) return ERANGE;
    if (errnum < 0 || errnum > sizeof(__errno_msgs)/sizeof(char *)) return EINVAL;
    if (buflen < 8) return ERANGE;
    if (__errno_msgs[errnum] == NULL) {
        strcpy(strerrbuf, "UNKNOWN");
        return 0;
    }
    if (buflen <= strlen(__errno_msgs[errnum])) return ERANGE;

    strcpy(strerrbuf, __errno_msgs[errno]);
    return 0;
}