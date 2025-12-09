// has to be here, because very early on we don't have interrupts so write() wouldn't work

#include <stdarg.h>

#include "../libc/src/include/string.h"
#include "../libc/src/include/stdio.h"
#include "../libc/src/include/ctype.h"
#include "include/kernel_tty.h"
#include "include/rs232.h"

#define vga_write(buf, count) vga_write(buf, count); com_write(1, buf, count);

extern char fmt_buf[PRINTF_MAX_FORMAT_OUT];

extern size_t fmt_handler_printf(const char * s, va_list * args);


void __attribute__((format(printf, 1, 2))) kprintf(const char * format, ...) {
    va_list args;
    va_start(args, format);

    const char * next_percent = strchrnul(format, '%');
    
    vga_write(format, next_percent-format);

    const char * temp_ptr;
    for (const char * i = next_percent; i < format + strlen(format); ) {
        i++; // character immediately following the %
        
        if (*i == 's') {
            temp_ptr = va_arg(args, const char *);
            vga_write(temp_ptr, strlen(temp_ptr));
            i++;
        } else {
            size_t inc = fmt_handler_printf(i, &args);
            i += inc;

            vga_write(fmt_buf, strlen(fmt_buf));
        }

        next_percent = strchrnul(i, '%');
        vga_write(i, next_percent-i);
        i = next_percent;
    }
    va_end(args);
}