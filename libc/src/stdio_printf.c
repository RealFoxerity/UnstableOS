#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include <wchar.h>
#include <errno.h>

#include <math.h>
#include <float.h>

// look into src/math/fpclassify.c for further info
#define LDBL_EXP_MASK 0x7FFF
static int printf_fpclassify_ld(long double f) {
    union {long double f; struct {uint64_t m; uint16_t e;};} u = {.f = f};
    u.e &= 0x7FFF;
    unsigned char msb = u.m >> 63;
    u.m = (u.m << 1) >> 1;
    if (u.e == 0 && !msb)    return u.m ? FP_SUBNORMAL : FP_ZERO;
    if (!msb)                return FP_NAN;
    if (u.e == LDBL_EXP_MASK)return u.m ? FP_NAN : FP_INFINITE;
    return FP_NORMAL;
}
#define LDBL_EXP_MASK  0x7FFF
#define LDBL_EXP_DELTA 0x3FFF

static long double __modfl(long double x, long double * iptr) {
    if (printf_fpclassify_ld(x) == FP_NAN) {
        *iptr = x;
        return x;
    }

    union {long double f; struct {uint64_t m; uint16_t e;};} u = {.f = x};
    int exp = u.e & LDBL_EXP_MASK;
    exp -= LDBL_EXP_DELTA;

    // the mantissa has no bits representing a whole number
    if (exp < 0) {
        *iptr = u.e & 0x8000 ? -0.0L : 0.0L;
        return x;
    }

    // the mantissa has no bits representing a fraction
    if (exp >= LDBL_MANT_DIG - 1) {
        nofrac:
        *iptr = x;
        return u.e & 0x8000 ? -0.0L : 0.0L;
    }

    // parts to the left correspond to whole numbers
    // -1 because of the integer bit present on x87
    uint64_t frac_part = (1ULL << (LDBL_MANT_DIG - exp - 1)) - 1;
    if ((u.m & frac_part) == 0)
        goto nofrac;

    u.m &= ~frac_part;
    *iptr = u.f;
    return x - u.f;
}

static void printf_float_itoa(long double f, char * out) {
    int ctr = 0;
    __modfl(f, &f);
    for (; ; ctr++) {
        out[ctr] = '0' + (unsigned int)f % 10;
        f /= 10;
        __modfl(f, &f);
        if (f == 0) break;
    }

    for (int i = 0; i <= ctr/2; i++) {
        char temp = out[i];
        out[i] = out[ctr-i];
        out[ctr-i] = temp;
    }
    out[ctr+1] = '\0';
}

static void ftoa(char * buf, long double f, unsigned int precision, char eng, char upper) {
    if (signbit(f)) {
        *buf++ = '-';
        f = -f;
    }
    switch (printf_fpclassify_ld(f)) {
        case FP_INFINITE:
            strcpy(buf, upper ? "INF" : "inf");
            return;
        case FP_NAN:
            strcpy(buf, upper ? "NAN" : "nan");
            return;
        default:
            break;
    }

    if (f > __PRINTF_MAX_FLOAT)
        eng = 1;

    int exp = 0;
    if (eng) {
        while (f >= 10) {
            f /= 10;
            exp++;
        }
        while (f < 1) {
            f *= 10;
            exp--;
        }
        itoad((int)f, buf);
    } else
        printf_float_itoa(f, buf);
    buf += strlen(buf);

    if (precision == 0)
        return;
    *buf++ = '.';
    f = __modfl(f, &(long double){0});
    for (size_t i = 0; i < precision; i++) {
        f *= 10;
        *buf++ = '0' + (uint64_t)f % 10;
    }
    if (!eng) {
        *buf = '\0';
        return;
    }
    *buf++ = upper ? 'E' : 'e';
    if (exp >= 0)
        *buf++ = '+';
    else {
        *buf++ = '-';
        exp = -exp;
    }
    if (exp < 10)
        *buf++ = '0';
    itoad(exp, buf);
}

enum width {
    W_NORMAL,
    W_L,
    W_LL,
    W_H,
    W_HH,
    W_Z, // size_t
    W_J, // intmax_t/uintmax_t
    W_T, // ptrdiff_t
    W_LD, // long double
};

// argument order if unnumbered is padding, precision, arg
struct fmt {
    unsigned long padding;    // (unsigned long)-1 if next argument
    unsigned long precision;  // (unsigned long)-1 if next argument
    unsigned long arg_offset; // for %n$ table construction, 0 if unnumbered (next)
    enum width width;
    char fmt; // this being 0 means invalid format
    unsigned char zeropad  : 1;
    unsigned char rightpad : 1; // aka left-justified
    unsigned char thousands: 1; // the ' grouping
    unsigned char sign     : 1; // the + argument
    unsigned char signpad  : 1; // the ' ' argument (preceding space if it would otherwise have '+')
    unsigned char alt_form : 1;
    // *m$ in %n$, padding/precision then becomes argument offset, width of these is sizeof(int)
    unsigned char padding_offset   : 1;
    unsigned char precision_offset : 1;
};

// returns the amount to move forward or 0 in case conversion failed for any reason
size_t __printf_get_fmt(const char * fmt, struct fmt * out) {
    if (*fmt != '%')
        return 0;
    memset(out, 0, sizeof(struct fmt));

    const char * orig_fmt = fmt;
    fmt++;
    char explicit_arg = 0;
    long temp = 0;
    char * end;

    // optional explicit argument offset
    if (isdigit(*fmt)) {
        temp = strtol(fmt, &end, 10);
        if (*end == '$') {
            // args are 1 indexed
            if (temp == 0)
                return 0;
            explicit_arg = 1;
            out->arg_offset = temp;
            fmt = end + 1;
        }
    }

    // flags
    while (1) {
        switch (*fmt) {
            case '\'':
                out->thousands = 1;
                break;
            case '-':
                out->rightpad = 1;
                break;
            case '+':
                out->sign = 1;
                break;
            case '#':
                out->alt_form = 1;
                break;
            case '0':
                out->zeropad = 1;
                break;
            case ' ':
                out->signpad = 1;
                break;
            default:
                goto done1;
        }
        fmt++;
    }
    done1:

    // minimum width
    if (*fmt != '*' && !isdigit(*fmt))
        goto prec;
    if (*fmt == '*') {
        fmt++;
        if (isdigit(*fmt)) {
            // either this is *n$, which would be invalid without explicit args
            // or this is some other integer, which isn't valid until precision
            if (!explicit_arg)
                return 0;
            out->padding = strtol(fmt, &end, 10);
            if (*end != '$' || out->padding == 0)
                return 0;
            out->padding_offset = 1;
            fmt = end + 1;
        } else if (explicit_arg)
            return 0;
        else
            out->padding = -1;
    } else {
        out->padding = strtol(fmt, &end, 10);
        fmt = end;
    }

    // precision
    prec:
    if (*fmt != '.')
        goto lenmod;
    fmt++;
    if (*fmt != '*' && !isdigit(*fmt))
        return 0;
    if (*fmt == '*') {
        fmt++;
        if (isdigit(*fmt)) {
            if (!explicit_arg)
                return 0;
            out->precision = strtol(fmt, &end, 10);
            if (*end != '$' || out->precision == 0)
                return 0;
            out->precision_offset = 1;
            fmt = end + 1;
        } else if (explicit_arg)
            return 0;
        else out->precision = -1;
    } else {
        out->precision = strtol(fmt, &end, 10);
        fmt = end;
    }


    // length modifier
    lenmod:
    switch (*fmt) {
        case 'h':
            fmt++;
            switch (*fmt) {
                case 'h':
                    out->width = W_HH;
                    break;
                default:
                    out->width = W_H;
                    goto format;
            }
            break;
        case 'l':
            fmt++;
            switch (*fmt) {
                case 'l':
                    out->width = W_LL;
                    break;
                default:
                    out->width = W_L;
                    goto format;
            }
            break;
        case 'j':
            out->width = W_J;
            break;
        case 'z':
            out->width = W_Z;
            break;
        case 't':
            out->width = W_T;
            break;
        case 'L':
            out->width = W_LD;
            break;
        default:
            goto format;
    }
    fmt++;

    format:
    switch (*fmt) {
        case 'C':
        case 'S':
            out->width = W_L;
            out->fmt = (char)tolower(*fmt);
            break;
        case '%':
        case 'd':
        case 'i':
        case 'o':
        case 'u':
        case 'x': case 'X':
        case 'f': case 'F':
        case 'e': case 'E':
        case 'g': case 'G':
        case 'a': case 'A':
        case 's':
        case 'c':
        case 'p':
        case 'n':
            out->fmt = *fmt;
            break;
        default:
            return 0;
    }
    return fmt - orig_fmt + 1;
}

size_t __printf_get_arg_offset(const unsigned char * width_table, size_t n) {
    assert(n);
    size_t out = 0;
    for (size_t i = 0; i < n - 1; i++) {
        if (i >= NL_ARGMAX) {
            return out + (n - i - 1) * sizeof(int); // technically UB, but why not
        }
        switch (width_table[i]) {
            default:
            case W_H:  // gets promoted
            case W_HH: // gets promoted
            case W_NORMAL:
                out += sizeof(int);
                break;
            case W_L:
                out += sizeof(long);
                break;
            case W_LL:
                out += sizeof(long long);
                break;
            case W_J:
                out += sizeof(intmax_t);
                break;
            case W_Z:
                out += sizeof(size_t);
                break;
            case W_T:
                out += sizeof(ptrdiff_t);
                break;
            case W_LD: // on x87 it's 80 bits, which gets aligned up to 96
                out += 12;
                break;
        }
    }
    return out;
}

// temporary buffer for the conversion (excl. padding)
// the format specification
// &(void*)va_list - to do increments in case of unnumbered arguments
// the amount of bytes written so far, used for %n
// table of NL_ARGMAX enum widths cast to uchar for the %n$ maths, in case n is larger than NL_ARGMAX, width is assumed to be 4
// returns amount in buf or -1 on failure
int __printf_handle_fmt(char * buf, struct fmt * fmt, void ** args, size_t written, const unsigned char * width_table) {
    intmax_t tempintmax;
    uintmax_t tempuintmax;
    void * target_arg;
    char * obuf = buf;

    unsigned int target_precision = -1;
    if (fmt->precision == -1) {
        target_precision = *(unsigned int*)*args;
        *args += sizeof(unsigned int);
    } else if (fmt->precision_offset) {
        target_precision = *(unsigned int*)(*args + __printf_get_arg_offset(width_table, fmt->precision));
    } else if (fmt->precision)
        target_precision = fmt->precision;

    if (target_precision != -1 && target_precision > __PRINTF_MAX_PREC)
        target_precision = __PRINTF_MAX_PREC;

    if (fmt->arg_offset)
        target_arg = *args + __printf_get_arg_offset(width_table, fmt->arg_offset);
    else
        target_arg = *args;

    switch (fmt->fmt) {
        case '%':
            *buf = '%';
            return 1;
        case 'd': case 'i':
            if (target_precision == -1)
                target_precision = 1;
            switch (fmt->width) {
                default:
                case W_NORMAL:
                case W_H: // promotion
                case W_HH: // promotion
                    tempintmax = *(int*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(int);
                    break;
                case W_L:
                    tempintmax = *(long*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(long);
                    break;
                case W_LL:
                    tempintmax = *(long long*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(long long);
                    break;
                case W_T:
                    tempintmax = *(intptr_t*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(intptr_t);
                    break;
                case W_J:
                    tempintmax = *(ptrdiff_t*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(ptrdiff_t);
                    break;
                case W_Z:
                    tempintmax = *(size_t*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(size_t);
                    break;
            }
            if (tempintmax >= 0 && fmt->signpad && !fmt->sign)
                *buf++ = ' ';
            if (tempintmax >= 0 && fmt->sign)
                *buf++ = '+';

            switch (fmt->fmt) {
                default:
                case 'd':
                case 'i':
                    itoad(tempintmax, buf);
                    break;
            }
            // prepare for the precision
            if (buf[0] == '-')
                buf++;
            // buf here on purpose because we don't want to dabble with the +/-/' '
            if (target_precision != -1 && strlen(buf) < target_precision) {
                size_t len = strlen(buf);
                size_t offset = target_precision - len;
                memmove(buf + offset, buf, len + 1);
                memset(buf, '0', offset);
            }
            return (int)strlen(obuf);
        case 'o':
        case 'u':
        case 'x': case 'X':
            if (target_precision == -1)
                target_precision = 1;
            switch (fmt->width) {
                default:
                case W_NORMAL:
                case W_H: // promotion
                case W_HH: // promotion
                    tempuintmax = *(unsigned int*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(unsigned int);
                    break;
                case W_L:
                    tempuintmax = *(unsigned long*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(unsigned long);
                    break;
                case W_LL:
                    tempuintmax = *(unsigned long long*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(unsigned long long);
                    break;
                case W_T:
                    tempuintmax = *(uintptr_t*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(uintptr_t);
                    break;
                case W_J:
                    tempuintmax = *(ptrdiff_t*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(ptrdiff_t);
                    break;
                case W_Z:
                    tempuintmax = *(size_t*)target_arg;
                    if (!fmt->arg_offset)
                        *args += sizeof(size_t);
                    break;
            }
            if (tempuintmax == 0) {
                fmt->fmt == 'o'; // to not do the #X special handling, 0 is not supposed to have 0x
                goto unsigned_prec;
            }

            switch (fmt->fmt) {
                default:
                case 'u':
                    itoaud(tempuintmax, buf);
                    break;
                case 'o':
                    if (fmt->alt_form)
                        *buf++ = '0';
                    itoao(tempuintmax, buf);
                    break;
                case 'x':
                case 'X':
                    if (fmt->alt_form) { // special handling required by caller with padding with 0
                        *buf++ = '0';
                        *buf++ = fmt->fmt;
                    }
                    itoax(tempuintmax, buf);
                    if (fmt->fmt == 'x') {
                        while (*buf) *buf++ = tolower(*buf);
                    }
                    break;
            }
            if ((fmt->fmt == 'x' || fmt->fmt == 'X') && fmt->alt_form) {
                target_precision = target_precision >= 2 ? target_precision - 2 : 0;
                obuf += 2; // temporarily remove/"hide" the 0x
            }
            unsigned_prec:
            if (strlen(obuf) < target_precision) {
                size_t len = strlen(obuf);
                size_t offset = target_precision - len;
                memmove(obuf + offset, obuf, len + 1);
                memset(obuf, '0', offset);
            }
            if ((fmt->fmt == 'x' || fmt->fmt == 'X') && fmt->alt_form)
                obuf -= 2; // restore back the 0x

            return (int)strlen(obuf);
        case 'f': case 'F':
        case 'e': case 'E':
            f:
            if (fmt->signpad || fmt->sign) {
                long double arg;
                if (fmt->width == W_LD)
                    arg = *(long double*)target_arg;
                else
                    arg = *(double*)target_arg;
                if (arg > 0)
                    *buf++ = fmt->sign ? '+' : ' ';
            }
            if (fmt->width == W_LD)
                ftoa(buf, *(long double*)target_arg, target_precision == -1 ? 6 : target_precision, tolower(fmt->fmt) == 'e', isupper(fmt->fmt));
            else
                ftoa(buf, *(double*)target_arg, target_precision == -1 ? 6 : target_precision, tolower(fmt->fmt) == 'e', isupper(fmt->fmt));
            if (!fmt->arg_offset)
                *args += fmt->width == W_LD ? 12 : sizeof(double); // long double is 80 bits, have to round up to 4 bytes
            return (int)strlen(obuf);
        case 'g': case 'G':
            // this isn't exactly correct, but will suffice for now, TODO: proper %G
            if (fmt->width == W_LD) {
                if (*(long double*)target_arg < 1e-4L)
                    fmt->fmt = isupper(fmt->fmt) ? 'E' : 'e';
                goto f;
            }
            if (*(double*)target_arg < 1e-4)
                fmt->fmt = isupper(fmt->fmt) ? 'E' : 'e';
            goto f;
        case 'c':
            // wchar conversions
            if (fmt->width == W_L) {
                wint_t value = *(wint_t*)target_arg;
                size_t ret = wcrtomb(buf, (wchar_t)value, &(mbstate_t){0});
                if (!fmt->arg_offset)
                    *args += sizeof(wint_t);
                return (int)ret;
            }

            *buf = *(char*)target_arg;
            if (!fmt->arg_offset)
                *args += sizeof(int); // argument promotion
            return 1;
        case 's':
            return 0; // to be handled by the caller
        case 'p':
            tempuintmax = *(uintptr_t*)target_arg;
            itoax(tempuintmax, buf);
            if (!fmt->arg_offset)
                *args += sizeof(uintptr_t);
            return (int)strlen(buf);
        case 'n':
            *(int*)target_arg = (int)written;
            if (!fmt->arg_offset)
                *args += sizeof(int);
            return 0;
        default:
            break;

        case 'a': case 'A': // hex floats
            assert(!"TODO: implement %A into printf\n");
    }
    return (int)strlen(buf);
}

int vfprintf(FILE * restrict stream, const char * restrict format, va_list args) {
    char fmt_buf[__PRINTF_MAX_FORMAT_OUT];
    unsigned char width_table[NL_ARGMAX];
    char wcrbuf[MB_LEN_MAX];

    void * arg = (void*)args;

    const char * next_percent = strchr(format, '%');
    struct fmt fmt = {0};
    while (next_percent) {
        size_t off = __printf_get_fmt(next_percent, &fmt);
        if (off == 0) {
            err:
            ___set_errno(EINVAL);
            return -1;
        }
        if (fmt.arg_offset) {
            switch (tolower(fmt.fmt)) {
                case 'a': case 'e': case 'f': case 'g':
                    if (fmt.width != W_LD) fmt.width = W_LL; // double is 64 bits
                    break;
                case 'c': case 's':
                case 'p':
                    fmt.width = W_NORMAL;
                default:
                    break;
            }
            if (fmt.arg_offset <= NL_ARGMAX)
                width_table[fmt.arg_offset - 1] = fmt.width;
        }
        if (fmt.padding_offset && fmt.padding <= NL_ARGMAX)
            width_table[fmt.padding - 1] = W_NORMAL;
        if (fmt.precision_offset && fmt.precision <= NL_ARGMAX)
            width_table[fmt.precision - 1] = W_NORMAL;

        next_percent += off;
        next_percent = strchr(next_percent, '%');
    }

    next_percent = strchrnul(format, '%');

    int total_written = 0;
    size_t written = -1;
    if (stream) {
        written = fwrite(format, 1, next_percent-format, stream);
        total_written += (int)written;
        if (written < next_percent-format)
            return total_written;
    } else
        total_written = next_percent - format;

    size_t len = 0;
    for (const char * i = next_percent; i < format + strlen(format); ) {
        memset(fmt_buf, 0, __PRINTF_MAX_FORMAT_OUT);
        size_t inc = __printf_get_fmt(i, &fmt);
        if (inc == 0)
            goto err;
        i += inc;

        unsigned int padding = -1;
        if (fmt.padding == -1) {
            padding = *(unsigned int*)arg;
            arg += sizeof(unsigned int);
        } else if (fmt.padding_offset) {
            padding = *(unsigned int*)(arg + __printf_get_arg_offset(width_table, fmt.padding));
        } else if (fmt.padding)
            padding = fmt.padding;

        if (fmt.fmt == 's') {
            // we can't handle strings inside the printf fmt handler
            // the 0 flag always gets ignored, as with ', ' ', '#', '+'
            unsigned int precision = -1;
            if (fmt.precision == -1) {
                precision = *(unsigned int*)arg;
                arg += sizeof(unsigned int);
            } else if (fmt.precision_offset) {
                precision = *(unsigned int*)(arg + __printf_get_arg_offset(width_table, fmt.precision));
            } else if (fmt.precision)
                precision = fmt.precision;

            void * target_arg;
            if (fmt.arg_offset)
                target_arg = arg + __printf_get_arg_offset(width_table, fmt.arg_offset);
            else {
                target_arg = arg;
                arg += sizeof(char*);
            }
            const wchar_t * wbuf = *(const wchar_t **)target_arg;
            const char * buf = *(const char **)target_arg;

            if (fmt.width == W_L) {
                len = wcsnlen(wbuf, precision);
            } else
                len = strnlen(buf, precision);
            if (padding != -1 && !fmt.rightpad && len < padding) {
                if (stream) {
                    for (size_t j = 0; j < padding - len; j++) {
                        if (fputc(' ', stream) == EOF)
                            return total_written;
                        total_written++;
                    }
                } else
                    total_written += (int)(padding - len);
            }

            if (fmt.width == W_L) {
                // for some reason there's no such thing as fwwrite :p
                for (size_t j = 0; j < len; j++) {
                    written = wcrtomb(wcrbuf, wbuf[j], &(mbstate_t){0});
                    if (written == -1)
                        return total_written;
                    if (stream && fputwc(wbuf[j], stream) == WEOF)
                        return total_written;
                    total_written += (int)written;
                }
            } else {
                if (stream) {
                    written = fwrite(buf, 1, len, stream);
                    if (written == -1)
                        return total_written;
                    total_written += (int)written;
                    if (written < len)
                        return total_written;
                } else
                    total_written += (int)len;
            }
            goto rpad;
        }

        len = __printf_handle_fmt(fmt_buf, &fmt, &arg, total_written, width_table);
        if (len == -1)
            return total_written;

        const char * __fmt_buf = fmt_buf; // we might need to change it when padding

        if (padding != -1 && !fmt.rightpad && len < padding) {
            if (fmt.zeropad) {
                switch (*__fmt_buf) {
                    case '+':
                    case '-':
                    case ' ':
                        if (stream && fputc(*__fmt_buf, stream) == EOF)
                            return total_written;
                        total_written ++;
                        len--;
                        __fmt_buf++;
                        padding = padding >= 1 ? padding - 1 : 0;
                    default:
                        // [+- ]0x always gets pushed to the left
                        if (!(fmt.alt_form && tolower(fmt.fmt) == 'x'))
                            break;
                        if (stream && (written = fwrite(__fmt_buf, 1, 2, stream)) != 2) {
                            if (written == -1)
                                return total_written;
                            return total_written + (int)written;
                        }
                        total_written += 2;
                        len -= 2;
                        __fmt_buf += 2;
                        padding = padding >= 2 ? padding - 2 : 0;
                }
            }
            if (stream) {
                for (size_t j = 0; j < padding - len; j++) {
                    if (fputc(fmt.zeropad ? '0' : ' ', stream) == EOF) {
                        return total_written;
                    }
                    total_written++;
                }
            } else
                total_written += (int)(padding - len);
        }
        if (stream)
            written = fwrite(__fmt_buf, 1, len, stream);
        else
            written = len;

        if (written == -1)
            return total_written;
        total_written += (int)written;
        if (written < len)
            return total_written;

        rpad:
        if (padding != -1 && fmt.rightpad && len < padding) {
            if (stream) {
                for (size_t j = 0; j < padding - len; j++) {
                    if (fputc(' ', stream) == EOF)
                        return total_written;
                    total_written++;
                }
            } else
                total_written += (int)(padding - len);
        }

        next_percent = strchrnul(i, '%');
        if (stream)
            written = fwrite(i, 1, next_percent-i, stream);
        else
            written = next_percent - i;

        if (written == -1)
            return total_written;
        total_written += (int)written;
        if (written < next_percent-i)
            return total_written;
        i = next_percent;
    }
    return total_written;
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
    return vsnprintf(s, -1, format, args);
}

int vsnprintf(char * restrict s, size_t size, const char * restrict format, va_list args) {
    if (size == 1) {
        *s = 0;
        return 1;
    }
    FILE * string = NULL;

    // if size is zero, snprint should return how much space it would require
    if (size != 0)
        string = fmemopen(s, size - 1, "w");

    int ret = vfprintf(string, format, args);
    if (ret < size - 1)
        s[ret] = 0;
    else
        s[size - 1] = 0;
    fclose(string);
    return ret;
}