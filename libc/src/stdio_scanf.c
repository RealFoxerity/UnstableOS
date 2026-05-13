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
        conv_end = NULL;
        if (isspace(format[i])) {
            while (isspace(s[++soff]));
            continue;
        }

        void * temp = NULL;
        if (format[i] == '%') {
            i++;
            if (format[i] == '%') { // literal %
                if (s[soff] != '%') return matched_args;
                soff++;
                continue;
            }

            temp = va_arg(args, unsigned short *);
            if (temp == NULL) {
                errno = EINVAL;
                return -1;
            }

            switch (format[i]) {
                case 'c': // char
                    *(char *)temp = s[soff];
                    soff++;
                    break;
                case 's': // string, not secure!
                    for (char * j = temp; !isspace(s[soff]) && s[soff] != '\0'; soff++, j++) *j = s[soff];
                    break;
                case 'h': // short
                    i++;
                    switch (format[i]) {
                        case 'd':
                            *(short*)temp = strtol(s + soff, &conv_end, 10);
                            break;
                        case 'u':
                            *(unsigned short*)temp = strtoul(s + soff, &conv_end, 10);
                            break;
                        case 'x':
                            *(unsigned short*)temp = strtoul(s + soff, &conv_end, 16);
                            break;
                        case 'h': // char
                            i++;
                            switch (format[i]) {
                                case 'd':
                                    *(char*)temp = strtol(s + soff, &conv_end, 10);
                                    break;
                                case 'u':
                                    *(unsigned char*)temp = strtoul(s + soff, &conv_end, 10);
                                    break;
                                case 'x':
                                    *(unsigned char*)temp = strtoul(s + soff, &conv_end, 16);
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
                    *(int*)temp = strtol(s + soff, &conv_end, 10);
                    break;
                case 'u': // unsigned int
                    *(unsigned int*)temp = strtoul(s + soff, &conv_end, 10);
                    break;
                case 'x':
                    *(unsigned int*)temp = strtoul(s + soff, &conv_end, 16);
                    break;
                case 'l': // 32+ bit numbers
                    i++;
                    switch (format[i]) {
                        case 'd':
                            *(long*)temp = strtol(s + soff, &conv_end, 10);
                            break;
                        case 'u':
                            *(unsigned long*)temp = strtoul(s + soff, &conv_end, 10);
                            break;
                        case 'x':
                            *(unsigned long*)temp = strtoul(s + soff, &conv_end, 16);
                            break;
                        case 'l':
                            i++;
                            switch (format[i]) {
                                case 'd':
                                    *(long long*)temp = strtoll(s + soff, &conv_end, 10);
                                    break;
                                case 'u':
                                    *(unsigned long long*)temp = strtoull(s + soff, &conv_end, 10);
                                    break;
                                case 'x':
                                    *(unsigned long long*)temp = strtoull(s + soff, &conv_end, 16);
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
                // TODO: float, double
            }
            if (s + soff == conv_end) return matched_args; // failed to parse
            if (conv_end != NULL) soff = conv_end - s - 1;
            matched_args++;
            continue;
        }

        if (format[i] != s[soff]) return matched_args;
        else soff ++;
    }

    return matched_args;
}
