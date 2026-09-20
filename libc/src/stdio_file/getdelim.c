#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>

ssize_t getdelim(char **restrict lineptr, size_t *restrict n, int delimiter, FILE *restrict stream) {
    if (delimiter < 0) {
        ___set_errno(EINVAL);
        return -1;
    }
    if (!*lineptr)
        *n = 0;

    flockfile(stream);
    ssize_t off = 0;
    while (off < SSIZE_MAX - 1) {
        int temp = getc_unlocked(stream);

        if (temp == EOF) {
            if (feof(stream))
                break;
            funlockfile(stream);
            return -1;
        }
        if (off + 1 >= *n) { // +1 for delim + nul
            if (*n < 32)
                *n = 32;

            void * new = realloc(*lineptr, *n + *n/2);

            if (!new) {
                ungetc(temp, stream);
                ___set_errno(ENOMEM);
                funlockfile(stream);
                return -1;
            }

            *n += *n/2;
            *lineptr = new;
        }
        (*lineptr)[off++] = temp;
        if (temp == delimiter) {
            break;
        }
    }

    (*lineptr)[off] = '\0';
    funlockfile(stream);

    return off;
}
ssize_t getline(char **restrict lineptr, size_t *restrict n, FILE *restrict stream) {
    return getdelim(lineptr, n, '\n', stream);
}