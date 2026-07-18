#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "errno_msgs.h"

#define ERRNO_STRING_LEN 64

void perror(const char * s) {
    static char errno_string[ERRNO_STRING_LEN] = "Unknown error";

    if (s != NULL) {
        fprintf(stderr, "%s: ", s);
    }
    strerror_r(___get_errno(), errno_string, ERRNO_STRING_LEN);
    fprintf(stderr, "%s\n", errno_string);
}

char *strerror(int errnum) {
    static char errno_string[ERRNO_STRING_LEN] = "Unknown error";
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

    strcpy(strerrbuf, __errno_msgs[errnum]);
    return 0;
}

#include <UnstableOS/syscalls.h>
#include <fcntl.h>

int rename(const char *old, const char *new) {
    if (!old || !new) {
        ___set_errno(EFAULT);
        return -1;
    }
    int ret = syscall(SYSCALL_RENAMEAT, AT_FDCWD, old, AT_FDCWD, new);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int renameat(int oldfd, const char *old, int newfd, const char *new) {
    int ret = syscall(SYSCALL_RENAMEAT, oldfd, old, newfd, new);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}