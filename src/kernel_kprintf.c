// has to be here, because very early on we don't have interrupts so write() wouldn't work

#include <stdarg.h>

#include "../libc/src/include/string.h"
#include "../libc/src/include/stdio.h"
#include "../libc/src/include/ctype.h"
#include "include/devs.h"
#include "include/fs/fs.h"
#include "include/kernel_tty.h"
#include "include/kernel_sched.h" // for kernel_task fd access
#include "include/kernel_tty_io.h"
#include "include/rs232.h"

void kprintf_write(const char * buf, size_t count) {
    if (__builtin_expect(kernel_task == NULL || kernel_task->fds[0] == NULL || kernel_task->fds[0]->inode == NULL, 0)) {
        vga_write(buf, count);
        com_write(0, buf, count);
    } else {
        tty_write(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE), buf, count);
    }
}

extern char fmt_buf[PRINTF_MAX_FORMAT_OUT];

extern size_t fmt_handler_printf(const char * s, va_list * args);


void __attribute__((format(printf, 1, 2))) kprintf(const char * format, ...) {
    va_list args;
    va_start(args, format);

    const char * next_percent = strchrnul(format, '%');
    
    kprintf_write(format, next_percent-format);

    const char * temp_ptr;
    for (const char * i = next_percent; i < format + strlen(format); ) {
        i++; // character immediately following the %
        
        if (*i == 's') {
            temp_ptr = va_arg(args, const char *);
            kprintf_write(temp_ptr, strlen(temp_ptr));
            i++;
        } else {
            size_t inc = fmt_handler_printf(i, &args);
            i += inc;

            kprintf_write(fmt_buf, strlen(fmt_buf));
        }

        next_percent = strchrnul(i, '%');
        kprintf_write(i, next_percent-i);
        i = next_percent;
    }
    va_end(args);
}