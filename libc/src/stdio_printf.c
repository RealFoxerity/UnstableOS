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

    if (isdigit(*s) || *s == '-') {
        padding = atoi(s);
        if (*s == '-') s++;
        while (isdigit(*s)) s++;
    }
    padding_delta -= (unsigned long)s;

    switch (*s) {
        case '%':
            fmt_buf[0] = '%';
            fmt_buf[1] = '\0';
            return padding_delta+1;
        case 'u':
        case 'd':
            if (*s == 'u') itoaud(va_arg(*args, uint32_t), fmt_buf);
            else itoad(va_arg(*args, uint32_t), fmt_buf);
            
            if (padding > (long)strlen(fmt_buf)) {
                if (padding + strlen(fmt_buf) + 1 > PRINTF_MAX_FORMAT_OUT) 
                    padding = PRINTF_MAX_FORMAT_OUT - strlen(fmt_buf) - 1;

                memmove(fmt_buf+padding, fmt_buf, strlen(fmt_buf)+1); // todo: check if correct
            }
            else if (-padding > (long)strlen(fmt_buf)) {
                if (-padding + strlen(fmt_buf) + 1 > PRINTF_MAX_FORMAT_OUT) 
                    padding = PRINTF_MAX_FORMAT_OUT - strlen(fmt_buf) - 1;
                
                fmt_buf[strlen(fmt_buf)+padding] = '\0';
                memset(fmt_buf+strlen(fmt_buf), ' ', padding);
            }
            return padding_delta+1;
        case 'h':
            if (!(*(s+1) == 'x' || (*(s+1) == 'h' && *(s+2) == 'x'))) {
                strcpy(fmt_buf, "INV_SPEC");
                return padding_delta+1;
            }
            if (*(s+1) == 'h') ctoax(va_arg(*args, int), fmt_buf); // int because minimum argument is always int
            else stoax(va_arg(*args, int), fmt_buf);
            fmt_buf[*(s+1) == 'h'?2:4] = '\0';
            return padding_delta + *(s+1) == 'h'?3:2;
        case 'x':
            itoax(va_arg(*args, uint32_t), fmt_buf);
            fmt_buf[8] = '\0';
            return padding_delta+1;
        case 'l':
            if (*(s+1) != 'x') {
                strcpy(fmt_buf, "INV_SPEC");
                return padding_delta+1;
            }
            i64toax(va_arg(*args, uint64_t), fmt_buf);
            fmt_buf[16] = '\0';
            return padding_delta+2;        
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
            return padding_delta+1;
        default:
            strcpy(fmt_buf, "INV_SPEC");
            return padding_delta+1;
    }

    return 0;
}

void __attribute__((format(printf, 1, 2))) printf(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(STDOUT, format, args);
}
void __attribute__((format(printf, 2, 3))) fprintf(unsigned int fd, const char * format, ...) {
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



void vfprintf(unsigned int fd, const char * format, va_list args) {
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