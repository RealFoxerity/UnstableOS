#include "include/ctype.h"
#include "include/stdio.h"
#include "include/stdlib.h"
#include "../../src/include/errno.h"
#include "include/unistd.h"
#include <stdarg.h>

int __attribute__((format(scanf, 1, 2))) scanf(const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    return vscanf(format, args);
}

int __attribute__((format(scanf, 2, 3))) fscanf(int fd, const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    return vfscanf(fd, format, args);
}

int __attribute__((format(scanf, 2, 3))) sscanf(const char * restrict s, const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    return vsscanf(s, format, args);
}
int vscanf(const char * restrict format, va_list args) {return vfscanf(STDIN_FILENO, format, args);}


#define VFSCANF_MAX_STRING 4096
int vfscanf(int fd, const char * restrict format, va_list args) {
    // TODO: do properly, i'm way too lazy :P
    // TODO: scanf should leave \n in the recv buffer - not consume it
    // and my barely functioning implementation just consumes it as a side effect of calling read()
    // TODO: increase max string, maybe chunk it and iterate args list?

    char * buf = malloc(VFSCANF_MAX_STRING);
    if (buf == NULL) {
        errno = ENOMEM;
        return -1;
    }

    size_t read_bytes = read(fd, buf, VFSCANF_MAX_STRING-1);
    if (read_bytes < 0) {
        free(buf);
        errno = EBADF;
        return -1;
    }

    buf[read_bytes] = '\0';

    int ret = vsscanf(buf, format, args);

    free(buf);

    return ret;
}

int vsscanf(const char * restrict s, const char * restrict format, va_list args) {
    // TODO: maybe EINVAL is not the best option?
    size_t soff = 0;
    int matched_args = 0;

    char * conv_end = NULL; // for stuff like strtoll

    for (size_t i = 0; format[i] != '\0'; i++) {
        if (isspace(format[i])) {
            while (isspace(s[++soff]));
            continue;
        }

        void * temp = NULL;
        if (format[i] == '%') {
            i++;
            switch (format[i]) {
                case '%': // literal %
                    if (s[soff] != '%') return matched_args;
                    soff++;
                    break;
                case 'c': // char
                    temp = va_arg(args, char *);
                    if (temp == NULL) {
                        errno = EINVAL;
                        return -1;
                    }

                    *(char *)temp = s[soff];
                    soff++;
                    matched_args ++;
                    break;
                case 's': // string, not secure!
                    temp = va_arg(args, char *);
                    if (temp == NULL) {
                        errno = EINVAL;
                        return -1;
                    }

                    for (char * j = temp; !isspace(s[soff]) && s[soff] != '\0'; soff++, j++) *j = s[soff];
                    matched_args++;
                    break;
                case 'h': // short
                    i++;
                    switch (format[i]) {
                        case 'd':
                            if (!isdigit(s[soff]) && s[soff] != '-') return matched_args;

                            temp = va_arg(args, short *);
                            if (temp == NULL) {
                                errno = EINVAL;
                                return -1;
                            }

                            *(short*)temp = strtol(s + soff, &conv_end);
                            soff = conv_end - s;
                            matched_args++;
                            break;
                        case 'u':
                            if (!isdigit(s[soff])) return matched_args;

                            temp = va_arg(args, unsigned short *);
                            if (temp == NULL) {
                                errno = EINVAL;
                                return -1;
                            }

                            *(short*)temp = strtoul(s + soff, &conv_end);
                            soff = conv_end - s;
                            matched_args++;
                            break;
                        case 'h': // char
                            i++;
                            switch (format[i]) {
                                case 'd':
                                    if (!isdigit(s[soff]) && s[soff] != '-') return matched_args;

                                    temp = va_arg(args, char *);
                                    if (temp == NULL) {
                                        errno = EINVAL;
                                        return -1;
                                    }

                                    *(char*)temp = (char)strtol(s + soff, &conv_end);
                                    soff = conv_end - s;
                                    matched_args++;
                                    break;
                                case 'u':
                                    if (!isdigit(s[soff])) return matched_args;

                                    temp = va_arg(args, unsigned char *);
                                    if (temp == NULL) {
                                        errno = EINVAL;
                                        return -1;
                                    }

                                    *(unsigned char*)temp = (unsigned char)strtoul(s + soff, &conv_end);
                                    soff = conv_end - s;
                                    matched_args++;
                                    break;
                                default:
                                    errno = EINVAL;
                                    return -1;
                            }
                            break;
                        default:
                            errno = EINVAL;
                            return -1;
                    }
                    break;
                case 'd': // signed int
                    normal_signed:
                    if (!isdigit(s[soff]) && s[soff] != '-') return matched_args;

                    temp = va_arg(args, int *);
                    if (temp == NULL) {
                        errno = EINVAL;
                        return -1;
                    }

                    *(int*)temp = (int)strtol(s + soff, &conv_end);
                    soff = conv_end - s;
                    matched_args++;
                    break;
                case 'u': // unsigned int
                    normal_unsigned:
                    if (!isdigit(s[soff])) return matched_args;

                    temp = va_arg(args, unsigned int *);
                    if (temp == NULL) {
                        errno = EINVAL;
                        return -1;
                    }

                    *(unsigned int*)temp = (unsigned int)strtoul(s + soff, &conv_end);
                    soff = conv_end - s;
                    matched_args++;
                    break;
                case 'l': // 32+ bit numbers
                    i++;
                    switch (format[i]) {
                        case 'd':
                            if (!isdigit(s[soff]) && s[soff] != '-') return matched_args;

                            temp = va_arg(args, long *);
                            if (temp == NULL) {
                                errno = EINVAL;
                                return -1;
                            }

                            *(long*)temp = strtol(s + soff, &conv_end);
                            soff = conv_end - s;
                            matched_args++;
                            break;
                        case 'u':
                            if (!isdigit(s[soff])) return matched_args;

                            temp = va_arg(args, unsigned long *);
                            if (temp == NULL) {
                                errno = EINVAL;
                                return -1;
                            }

                            *(unsigned long*)temp = strtoul(s + soff, &conv_end);
                            soff = conv_end - s;
                            matched_args++;
                            break;
                        case 'l':
                            i++;
                            switch (format[i]) {
                                case 'd':
                                    if (!isdigit(s[soff]) && s[soff] != '-') return matched_args;

                                    temp = va_arg(args, long long *);
                                    if (temp == NULL) {
                                        errno = EINVAL;
                                        return -1;
                                    }

                                    *(long long*)temp = (long long)strtoll(s + soff, &conv_end);
                                    soff = conv_end - s;
                                    matched_args++;
                                    break;
                                case 'u':
                                    if (!isdigit(s[soff])) return matched_args;

                                    temp = va_arg(args, unsigned long long *);
                                    if (temp == NULL) {
                                        errno = EINVAL;
                                        return -1;
                                    }

                                    *(unsigned long long*)temp = (unsigned long long)strtoull(s + soff, &conv_end);
                                    soff = conv_end - s;
                                    matched_args++;
                                    break;
                                default:
                                    errno = EINVAL;
                                    return -1;
                            }
                            break;
                        default:
                            errno = EINVAL;
                            return -1;
                    }
                    break;
                default:
                    errno = EINVAL;
                    return -1;
                // TODO: add hexadecimal, float, double
            }
            continue;
        }

        if (format[i] != s[soff]) return matched_args;
        else soff ++;
    }

    return matched_args;
}
