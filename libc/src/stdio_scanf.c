#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>

#include <string.h>
#include <pthread.h>
#include <assert.h>
int __attribute__((format(scanf, 1, 2))) scanf(const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vscanf(format, args);
    va_end(args);
    return ret;
}

int __attribute__((format(scanf, 2, 3))) fscanf(FILE * restrict stream, const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfscanf(stream, format, args);
    va_end(args);
    return ret;
}

int __attribute__((format(scanf, 2, 3))) sscanf(const char * restrict s, const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsscanf(s, format, args);
    va_end(args);
    return ret;
}
int vscanf(const char * restrict format, va_list args) {return vfscanf(stdin, format, args);}


#define MAX_INT_LEN 32 // more than enough to represent the longest possible uint64
long long stroll_stdio(FILE * stream, unsigned char base, char * success) {
    char input[MAX_INT_LEN] = {0};
    for (int i = 0; i < MAX_INT_LEN; i++) {
        int c = fgetc(stream);
        if (c == EOF && i == 0)
            return *success = 0;
        if (c == EOF)
            break;
        if (c != '+' && c != '-' && !isdigit((char)c)) {
            if (!(base == 16 && tolower((char)c) >= 'a' && tolower((char)c) <= 'f')) {
                ungetc(c, stream);
                break;
            }
        }
        input[i] = (char)c;
    }

    long long out = strtoll(input, NULL, base);
    *success = 1;
    return out;
}

static int vfscanf_unlocked(FILE * restrict stream, const char * restrict format, va_list args) {
    int matched_args = 0;

    char conv_success = 0;


    for (size_t i = 0; format[i] != '\0'; i++) {
        int c = EOF;

        if (isspace(format[i])) {
            while (isspace((char)(c = fgetc(stream)))) {}
            if (c == EOF || ungetc(c, stream) != c)
                return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
            continue;
        }

        void * temp = NULL;

        if (format[i] == '%') {
            long long temp_int = 0;
            unsigned long long temp_ull = 0;

            i++;
            if (format[i] == '%') { // literal %
                if ((c = fgetc(stream) != '%')) {
                    ungetc(c, stream);
                    return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                }
                continue;
            }

            while (isspace((char)(c = fgetc(stream)))) {}
            if (c == EOF || ungetc(c, stream) != c)
                return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;

            temp = va_arg(args, unsigned long *);
            if (temp == NULL) {
                ___set_errno(EINVAL);
                return matched_args == 0 ? EOF : matched_args;
            }

            switch (format[i]) {
                case 'c': // char
                    c = fgetc(stream);
                    if (c == EOF)
                        return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                    *(char *)temp = (char)c;
                    break;
                case 's': // string, not secure!
                    for (char * j = temp; !isspace((char)(c = fgetc(stream))) && c != '\0'; j++) *j = (char)c;
                    if (c == EOF || ungetc(c, stream) != c)
                        return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                    break;
                case 'h': // short
                    i++;
                    switch (format[i]) {
                        case 'd':
                            temp_int = (short)stroll_stdio(stream, 10, &conv_success);
                            if (conv_success == 0)
                                return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                            *(short*)temp = (short)temp_int;
                            break;
                        case 'x':
                        case 'u':
                            temp_int = (unsigned short)stroll_stdio(stream, format[i] == 'u' ? 10 : 16, &conv_success);
                            if (conv_success == 0)
                                return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                            *(unsigned short*)temp = (unsigned short)temp_int;
                            break;
                        case 'h': // char
                            i++;
                            switch (format[i]) {
                                case 'd':
                                    temp_int = (char)stroll_stdio(stream, 10, &conv_success);
                                    if (conv_success == 0)
                                        return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                                    *(char*)temp = (char)temp_int;
                                    break;
                                case 'x':
                                case 'u':
                                    temp_int = (unsigned char)stroll_stdio(stream, format[i] == 'u' ? 10 : 16, &conv_success);
                                    if (conv_success == 0)
                                        return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                                    *(unsigned char*)temp = (unsigned char)temp_int;
                                    break;
                                default:
                                    ___set_errno(EINVAL);
                                    return matched_args == 0 ? EOF : matched_args;
                            }
                            break;
                        default:
                            ___set_errno(EINVAL);
                            return matched_args == 0 ? EOF : matched_args;
                    }
                    break;
                case 'd':
                    temp_int = (int)stroll_stdio(stream, 10, &conv_success);
                    if (conv_success == 0)
                        return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                    *(int*)temp = (int)temp_int;
                    break;
                case 'x':
                case 'u':
                    temp_int = (unsigned int)stroll_stdio(stream, format[i] == 'u' ? 10 : 16, &conv_success);
                    if (conv_success == 0)
                        return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                    *(unsigned int*)temp = (unsigned int)temp_int;
                    break;
                case 'l': // 32+ bit numbers
                    i++;
                    switch (format[i]) {
                        case 'd':
                            temp_int = (long)stroll_stdio(stream, 10, &conv_success);
                            if (conv_success == 0)
                                return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                            *(long*)temp = (long)temp_int;
                            break;
                        case 'x':
                        case 'u':
                            temp_int = (unsigned long)stroll_stdio(stream, format[i] == 'u' ? 10 : 16, &conv_success);
                            if (conv_success == 0)
                                return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                            *(unsigned long*)temp = (unsigned long)temp_int;
                            break;
                        case 'l':
                            i++;
                            switch (format[i]) {
                                case 'd':
                                    temp_int = stroll_stdio(stream, 10, &conv_success);
                                    if (conv_success == 0)
                                        return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                                    *(long long*)temp = temp_int;
                                    break;
                                case 'x':
                                case 'u':
                                    temp_ull = (unsigned long long)stroll_stdio(stream, format[i] == 'u' ? 10 : 16, &conv_success);
                                    if (conv_success == 0)
                                        return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
                                    *(unsigned long long*)temp = temp_ull;
                                    break;
                                default:
                                    ___set_errno(EINVAL);
                                    return matched_args == 0 ? EOF : matched_args;
                            }
                            break;
                        default:
                            ___set_errno(EINVAL);
                            return matched_args == 0 ? EOF : matched_args;
                    }
                    break;
                default:
                    ___set_errno(EINVAL);
                    return matched_args == 0 ? EOF : matched_args;
                // TODO: float, double
            }
            matched_args++;
            continue;
        }

        if (format[i] != (c = fgetc(stream))) {
            ungetc(c, stream);
            return (matched_args == 0 || ferror(stream)) ? EOF : matched_args;
        }
    }

    return matched_args;
}

int vfscanf(FILE * restrict stream, const char * restrict format, va_list args) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    if (format == NULL) {
        ___set_errno(EINVAL);
        return -1;
    }

    assert(!pthread_mutex_lock(&stream->mutex));
    int ret = vfscanf_unlocked(stream, format, args);
    pthread_mutex_unlock(&stream->mutex);
    return ret;
}
int vsscanf(const char * restrict s, const char * restrict format, va_list args) {
    FILE * stream = fmemopen((void*)s, strlen(s) + 1, "r");

    int ret = vfscanf(stream, format, args);
    fclose(stream);
    return ret;
}
