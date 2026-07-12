#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "include/ctype.h"
#include "include/unistd.h"
#include <stdarg.h>
#include <stdint.h>

// EXTREMELY bad implementation of *printf functions, TODO: completely rewrite at some point

#define PRINTF_MAX_FORMAT_OUT 128 // assuming no format is going to produce longer text than 32 chars

ssize_t fmt_handler_printf(char * fmt_buf, const char * s, va_list * args) { // caller has to call va_arg themselves!
    int padding = 0;
    int precision = 0; // currently zeroes integers
    const char * temp_ptr;

    unsigned long padding_delta = (unsigned long)s;
    char zerofill = 0;
    if (*s == '0') {
        zerofill = 1;
        s++;
    }
    if (isdigit(*s) || *s == '-') {
        padding = atoi(s);
        if (*s == '-') s++;
        while (isdigit(*s)) s++;
    }
    if (*s == '.') {
        s++;
        precision = atoi(s);
        if (precision < 0) precision = -precision;
        if (*s == '-') s++;
        while (isdigit(*s)) s++;
    }

    if (precision > PRINTF_MAX_FORMAT_OUT) precision = PRINTF_MAX_FORMAT_OUT;

    padding_delta = (unsigned long)s - padding_delta;
    size_t curr_fmt_len;
    switch (*s) {
        case '%':
            fmt_buf[0] = '%';
            fmt_buf[1] = '\0';
            break;
        case 'u':
        case 'd':
            dec:
            if (*s == 'u') itoaud(va_arg(*args, uint32_t), fmt_buf);
            else itoad(va_arg(*args, uint32_t), fmt_buf);
            int_prec:
            curr_fmt_len = strlen(fmt_buf);
            if (curr_fmt_len < precision) {
                precision -= curr_fmt_len;
                memmove(fmt_buf+precision, fmt_buf, curr_fmt_len+1);
                memset(fmt_buf, '0', precision);
            }
            break;
        case 'p':
            temp_ptr = va_arg(*args, void*);
            if (temp_ptr == NULL) {
                strcpy(fmt_buf, "(nil)");
                break;
            }
            itoax((unsigned long)temp_ptr, fmt_buf);
            fmt_buf[8] = '\0';
            goto int_prec;
        case 'x':
            hex:
            itoax(va_arg(*args, uint32_t), fmt_buf);
            fmt_buf[8] = '\0';
            goto int_prec;
        case 'l': // explicitly 32+ bit numbers
            // since we do 32 bits by default, ld lu lx is the same as d u x
            s++;
            padding_delta++;
            switch (*s) {
                case 'x':
                    goto hex;
                case 'd':
                case 'u':
                    goto dec;
                case 'l': // 64 bit ints
                    s++;
                    padding_delta++;
                    switch (*s) {
                        case 'x': // 64 bit
                            i64toax(va_arg(*args, uint64_t), fmt_buf);
                            fmt_buf[16] = '\0';
                            goto int_prec;
                        case 'd':
                        case 'u':
                            if (*s == 'u') itoaud(va_arg(*args, uint64_t), fmt_buf);
                            else itoad(va_arg(*args, uint64_t), fmt_buf);
                            goto int_prec;
                        default: goto inv_spec;
                    }
                    break;
                default: goto inv_spec;
            }
            break;
        case 'h':
            s++;
            padding_delta++;
            switch (*s) {
                case 'x':
                    itoax((unsigned short)va_arg(*args, uint32_t), fmt_buf);
                    fmt_buf[4] = '\0';
                    goto int_prec;
                case 'u':
                case 'd':
                    if (*s == 'u') itoaud((unsigned short)va_arg(*args, uint32_t), fmt_buf);
                    else            itoad((short)va_arg(*args, uint32_t), fmt_buf);
                    goto int_prec;
                case 'h':
                    s++;
                    padding_delta++;
                    switch (*s) {
                        case 'x':
                            itoax((unsigned char)va_arg(*args, uint32_t), fmt_buf);
                            fmt_buf[2] = '\0';
                            goto int_prec;
                        case 'u':
                        case 'd':
                            if (*s == 'u') itoaud((unsigned char)va_arg(*args, uint32_t), fmt_buf);
                            else            itoad((char)va_arg(*args, uint32_t), fmt_buf);
                            goto int_prec;
                        default: goto inv_spec;
                    }
                    break;
                default: goto inv_spec;
            }
            break;
        case 's': // has to be handled by the specific printf call
            return -precision - 1;
        case 'c':
            fmt_buf[0] = va_arg(*args, int);
            fmt_buf[1] = '\0';
            break;
        // TODO: add float, double
        default: goto inv_spec;
    }

    // pad
    if (padding >= 0 && padding <= strlen(fmt_buf)) return padding_delta + 1; // nothing to pad
    if (padding < 0 && -padding <= strlen(fmt_buf)) return padding_delta + 1;

    if (padding > (long)strlen(fmt_buf)) {
        if (padding + strlen(fmt_buf) + 1 > PRINTF_MAX_FORMAT_OUT)
            padding = PRINTF_MAX_FORMAT_OUT - strlen(fmt_buf) - 1;

        int old_len = strlen(fmt_buf);
        memmove(fmt_buf+padding-old_len, fmt_buf, old_len+1);
        memset(fmt_buf, zerofill?'0':' ', padding-old_len);
    }
    else if (-padding > (long)strlen(fmt_buf)) {
        padding = -padding;
        if (padding + strlen(fmt_buf) + 1 > PRINTF_MAX_FORMAT_OUT)
            padding = PRINTF_MAX_FORMAT_OUT - strlen(fmt_buf) - 1;

        fmt_buf[padding] = '\0';
        memset(fmt_buf+strlen(fmt_buf), zerofill?'0':' ', padding-strlen(fmt_buf));
    }
    return padding_delta + 1;

    inv_spec:
    strcpy(fmt_buf, "INV_SPEC");
    return padding_delta+1;
}

int __attribute__((format(printf, 1, 2))) printf(const char * format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfprintf(stdout, format, args);
    va_end(args);
    return ret;
}
int __attribute__((format(printf, 2, 3))) fprintf(FILE * restrict stream, const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfprintf(stream, format, args);
    va_end(args);
    return ret;
}

int __attribute__((format(printf, 2, 3))) sprintf(char * restrict s, const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsprintf(s, format, args);
    va_end(args);
    return ret;
}

int __attribute__((format(printf, 3, 4))) snprintf(char * restrict s, size_t size, const char * restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(s, size, format, args);
    va_end(args);
    return ret;
}

int vsprintf(char * restrict s, const char * restrict format, va_list args) {
    char fmt_buf[PRINTF_MAX_FORMAT_OUT];

    const char * next_percent = strchrnul(format, '%');

    size_t off = 0;
    memcpy(s+off, format, next_percent-format);
    off += next_percent-format;

    for (const char * i = next_percent; i < format + strlen(format); ) {
        i++; // character immediately following the %

        ssize_t inc = fmt_handler_printf(fmt_buf, i, &args);
        const char * buf = fmt_buf;
        size_t len = strlen(buf);
        if (inc < 0) {
            buf = va_arg(args, const char *);
            len = strlen(buf);
            if (inc < -1) {
                if (len > -inc - 1)
                    len = -inc - 1;
            }

            while (*i++ != 's') {}
        } else {
            i += inc;
        }

        memcpy(s+off, buf, len);
        off += len;

        next_percent = strchrnul(i, '%');
        memcpy(s+off, i, next_percent-i);
        off += next_percent-i;

        i = next_percent;
    }
    s[off] = '\0';
    return (int)off;
}

int vsnprintf(char * restrict s, size_t size, const char * restrict format, va_list args) {
    char fmt_buf[PRINTF_MAX_FORMAT_OUT];

    const char * next_percent = strchrnul(format, '%');
    size --; // to do s[size] for \0

    size_t off = 0;
    if (next_percent-format >= size) {
        memcpy(s+off, format, size);
        s[size] = '\0';
        return (int)size;
    }

    memcpy(s+off, format, next_percent-format);
    off += next_percent-format;

    for (const char * i = next_percent; i < format + strlen(format); ) {
        i++; // character immediately following the %

        ssize_t inc = fmt_handler_printf(fmt_buf, i, &args);
        const char * buf = fmt_buf;
        size_t len = strlen(buf);
        if (inc < 0) { // smuggled string precision from fmt handler
            buf = va_arg(args, const char *);
            len = strlen(buf);
            if (inc < -1) {
                if (len > -inc - 1)
                    len = -inc - 1;
            }
            while (*i++ != 's') {}
        } else {
            i += inc;
        }

        if (off + len >= size) {
            memcpy(s+off, buf, size - off);
            s[size] = '\0';
            return (int)size;
        }
        memcpy(s+off, buf, len);
        off += len;

        next_percent = strchrnul(i, '%');

        if (off + next_percent-i >= size) {
            memcpy(s+off, i, size - off);
            s[size] = '\0';
            return (int)size;
        }
        memcpy(s+off, i, next_percent-i);
        off += next_percent-i;

        i = next_percent;
    }
    s[off] = '\0';
    return (int)off;
}


int vfprintf(FILE * restrict stream, const char * restrict format, va_list args) {
    char fmt_buf[PRINTF_MAX_FORMAT_OUT];

    const char * next_percent = strchrnul(format, '%');

    int total_written = 0;
    size_t written = -1;
    written = fwrite(format, 1, next_percent-format, stream);
    total_written += (int)written;
    if (written < next_percent-format)
        return total_written;

    const char * temp_ptr;
    size_t len = 0;
    for (const char * i = next_percent; i < format + strlen(format); ) {
        i++; // character immediately following the %

        ssize_t inc = fmt_handler_printf(fmt_buf, i, &args);
        const char * buf = fmt_buf;
        len = strlen(buf);

        if (inc < 0) { // smuggled string precision from fmt handler
            buf = va_arg(args, const char *);
            len = strlen(buf);
            if (inc < -1) {
                if (len > -inc - 1)
                    len = -inc - 1;
            }
            while (*i++ != 's') {}
        } else {
            i += inc;
        }

        written = fwrite(buf, 1, len, stream);
        if (written == -1)
            return total_written;
        total_written += (int)written;
        if (written < len)
            return total_written;

        next_percent = strchrnul(i, '%');
        written = fwrite(i, 1, next_percent-i, stream);
        if (written == -1)
            return total_written;
        total_written += (int)written;
        if (written < next_percent-i)
            return total_written;
        i = next_percent;
    }
    return total_written;
}