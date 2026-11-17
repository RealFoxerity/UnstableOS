#include <stdarg.h>
#include <ctype.h>
#include <wctype.h>
#include <string.h>
#include <stdio.h>
#include "kernel.h"
#include "kernel_console.h"
#include "rs232.h"

void kprintf_write(const char * buf, size_t count);

static inline void print_time() {
    char curr_uptime[11] = {0}; // a little bird told me that 32 bit integers never surpass 11 chars
    itoaud(uptime_clicks, curr_uptime);

    kprintf_write("[", 1);
    kprintf_write(curr_uptime, strlen(curr_uptime));
    for (int i = 0; i < 8 - strlen(curr_uptime); i++) {
        kprintf_write(" ", 1);
    }
    kprintf_write("] ", 2);
}


void kprintf_write(const char * buf, size_t count) { // TODO: rewrite, this is horrible...
    static char do_print_time = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        if (do_print_time) {
            do_print_time = 0;
            print_time();
        }
        if (buf[i] == '\n') do_print_time = 1;

        /*
        Normally, we would use the TTY system for the kernel log, however
        we want (and it's important) for the TTYs to be preemptible
        Considering kprintf mostly works as a debug tool to observe
        the internal state of the kernel, I have decided to move it
        from the TTY subsystem to raw com and vga write routines to
        avoid numerous deadlocks coming from printing from within
        interrupted TTY calls (for example kernel_create_thread()
        call inside the ps/2 driver interrupting tty getch or putch)
        */

        //if (__builtin_expect(kernel_task == NULL || kernel_task->fds[0] == NULL || kernel_task->fds[0]->inode == NULL, 0)) {
            // the kernel doesn't use the tty subsystem and its ONLCR flag, so we need to emulate it
            if (buf[i] == '\n') {
                console_write(&(char){'\r'}, 1);
                com_write(0, &(char){'\r'}, 1);
            }
            console_write(buf+i, 1);
            com_write(0, buf+i, 1);
        //} else {
        //    tty_write(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE), buf+i, 1);
        //}
    }
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

extern int __printf_handle_fmt(char * buf, struct fmt * fmt, void ** args, size_t written, const unsigned char * width_table);
extern size_t __printf_get_fmt(const char * fmt, struct fmt * out);
extern size_t __printf_get_arg_offset(const unsigned char * width_table, size_t n);

void __attribute__((format(printf, 1, 2))) kprintf(const char * format, ...) {
    va_list args;
    va_start(args, format);

    char fmt_buf[__PRINTF_MAX_FORMAT_OUT];
    unsigned char width_table[NL_ARGMAX];
    char wcrbuf[MB_LEN_MAX];

    void * arg = (void*)args;
    va_end(args);

    const char * next_percent = strchr(format, '%');
    struct fmt fmt = {0};
    while (next_percent) {
        size_t off = __printf_get_fmt(next_percent, &fmt);
        if (off == 0)
            return;
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

    kprintf_write(format, next_percent-format);

    size_t len = 0;
    for (const char * i = next_percent; i < format + strlen(format); ) {
        memset(fmt_buf, 0, __PRINTF_MAX_FORMAT_OUT);
        size_t inc = __printf_get_fmt(i, &fmt);
        if (inc == 0)
            return;
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
                for (size_t j = 0; j < padding - len; j++) {
                    kprintf_write(" ", 1);
                }
            }

            if (fmt.width == W_L) {
                // for some reason there's no such thing as fwwrite :p
                for (size_t j = 0; j < len; j++) {
                    size_t written = wcrtomb(wcrbuf, wbuf[j], &(mbstate_t){0});
                    if (written == -1)
                        return;
                    kprintf_write(wcrbuf, written);
                }
            } else {
                kprintf_write(buf, len);
            }
            goto rpad;
        }

        // kprintf probably doesn't need %n support
        len = __printf_handle_fmt(fmt_buf, &fmt, &arg, 0, width_table);
        if (len == -1)
            return;

        const char * __fmt_buf = fmt_buf; // we might need to change it when padding

        if (padding != -1 && !fmt.rightpad && len < padding) {
            if (fmt.zeropad) {
                switch (*__fmt_buf) {
                    case '+':
                    case '-':
                    case ' ':
                        kprintf_write(__fmt_buf, 1);
                        len--;
                        __fmt_buf++;
                        padding = padding >= 1 ? padding - 1 : 0;
                    default:
                        // [+- ]0x always gets pushed to the left
                        if (!(fmt.alt_form && tolower(fmt.fmt) == 'x'))
                            break;
                        kprintf_write(__fmt_buf, 2);
                        len -= 2;
                        __fmt_buf += 2;
                        padding = padding >= 2 ? padding - 2 : 0;
                }
            }
            for (size_t j = 0; j < padding - len; j++) {
                kprintf_write(fmt.zeropad ? "0" : " ", 1);
            }
        }
        kprintf_write(__fmt_buf, len);

        rpad:
        if (padding != -1 && fmt.rightpad && len < padding) {
            for (size_t j = 0; j < padding - len; j++) {
                kprintf_write(" ", 1);
            }
        }

        next_percent = strchrnul(i, '%');
        kprintf_write(i, next_percent-i);

        i = next_percent;
    }
}