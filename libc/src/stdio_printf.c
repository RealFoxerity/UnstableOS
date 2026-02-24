#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "include/ctype.h"
#include <stdarg.h>
#include <stdint.h>

// EXTREMELY bad implementation of *printf functions, TODO: completely rewrite at some point

#define PRINTF_MAX_FORMAT_OUT 128 // assuming no format is going to produce longer text than 32 chars


char fmt_buf[PRINTF_MAX_FORMAT_OUT];

size_t fmt_handler_printf(const char * s, va_list * args) { // caller has to call va_arg themselves!
    int padding = 0;
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
    padding_delta = (unsigned long)s - padding_delta;

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
            break;
        case 'h':
            if (!(*(s+1) == 'x' || (*(s+1) == 'h' && *(s+2) == 'x'))) goto inv_spec;
            if (*(s+1) == 'h') ctoax(va_arg(*args, int), fmt_buf); // int because minimum argument is always int
            else stoax(va_arg(*args, int), fmt_buf);
            fmt_buf[*(s+1) == 'h'?2:4] = '\0';
            padding_delta += ((*(s+1) == 'h')?2:1);
            break;
        case 'p':
            temp_ptr = va_arg(*args, void*);
            if (temp_ptr == NULL) {
                strcpy(fmt_buf, "(nil)");
                break;
            }
            itoax((unsigned long)temp_ptr, fmt_buf);
            fmt_buf[8] = '\0';
            break;
        case 'x':
            hex:
            itoax(va_arg(*args, uint32_t), fmt_buf);
            fmt_buf[8] = '\0';
            break;
        case 'l': // explicitily 32+ bit numbers
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
                            break;
                        case 'd':
                        case 'u':
                            goto dec; // TODO: bigger ints?
                        default: goto inv_spec;
                    }
                    break;
                default: goto inv_spec;
            }
            break;
        //case 's': // has to be handled by the specific printf call
        //    temp_ptr = va_arg(args, const char *);
        //    if (-padding > (long)strlen(fmt_buf)) { // only doing right-side padding
        //        if (padding >= PRINTF_MAX_FORMAT_OUT) padding = PRINTF_MAX_FORMAT_OUT - 1;
        //        memset(fmt_buf, ' ', padding);
        //        fmt_buf[padding] = '\0';
        //    }
        //    return temp_ptr;
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

void __attribute__((format(printf, 1, 2))) printf(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(STDOUT, format, args);
}
void __attribute__((format(printf, 2, 3))) fprintf(int fd, const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(fd, format, args);
}

void __attribute__((format(printf, 2, 3))) sprintf(char * s, const char * format, ...) {
    va_list args;
    va_start(args, format);
    vsprintf(s, format, args);
}

void vsprintf(char * s, const char * format, va_list args) {
    const char * next_percent = strchrnul(format, '%');
    
    size_t off = 0;
    memcpy(s+off, format, next_percent-format);
    off += next_percent-format;

    const char * temp_ptr;
    for (const char * i = next_percent; i < format + strlen(format); ) {
        i++; // character immediately following the %
        
        if (*i == 's') {
            temp_ptr = va_arg(args, const char *);
            memcpy(s+off, temp_ptr, strlen(temp_ptr));
            off += strlen(temp_ptr);
            i++;
        } else {
            size_t inc = fmt_handler_printf(i, &args);
            i += inc;
            
            memcpy(s+off, fmt_buf, strlen(fmt_buf));
            off += strlen(fmt_buf);
        }


        next_percent = strchrnul(i, '%');
        memcpy(s+off, i, next_percent-i);
        off += next_percent-i;
        
        i = next_percent;
    }
    s[off] = '\0';
    va_end(args);
}



void vfprintf(int fd, const char * format, va_list args) {
    const char * next_percent = strchrnul(format, '%');
    
    write(fd, format, next_percent-format);

    const char * temp_ptr;
    for (const char * i = next_percent; i < format + strlen(format); ) {
        i++; // character immediately following the %
        
        if (*i == 's') {
            temp_ptr = va_arg(args, const char *);
            write(fd, temp_ptr, strlen(temp_ptr));
            i++;
        } else {
            size_t inc = fmt_handler_printf(i, &args);
            i += inc;

            write(fd, fmt_buf, strlen(fmt_buf));
        }

        next_percent = strchrnul(i, '%');
        write(fd, i, next_percent-i);
        i = next_percent;
    }
    va_end(args);
}