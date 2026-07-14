#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/unistd.h"
#include "include/string.h"

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