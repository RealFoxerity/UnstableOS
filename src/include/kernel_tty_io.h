#ifndef KERNEL_TTY_IO_H
#define KERNEL_TTY_IO_H
#include "kernel_sched.h"
#include "kernel_ioctl.h"

#define __TTY_SERIAL 8
#define __TTY_CONSOLE 4
#define __TTY_PTY 20
#define TTY_LIMIT_KERNEL (__TTY_SERIAL + __TTY_CONSOLE + __TTY_PTY) // maximum amount of ttys opened
#define TTY_BUFFER_SIZE 4096

#define TTY_L_ECHO 1
#define TTY_L_ICANON 2 // new line buffering
#define TTY_L_ISIG 4 // enable signals
#define TTY_L_TOSTOP 8 // set SIGTTOU if background process group tries to write()

// as you might have noticed, i pick from the posix standard based on how easy things are to implement
#define TTY_I_CRNL 1 // carriage return -> new line
#define TTY_I_IGNCR 2 // ignore carriage return 
#define TTY_I_NLCR 4 // new line -> carriage return
#define TTY_I_ISTRIP 8 // strip 8 bit ascii to 7 bit

#define TTY_O_NLCR 1 // new line -> carriage return
#define TTY_O_CRNL 2 // carriage return -> new line
#define TTY_O_NLRET 4 // new line is carriage return


enum termios_control_chars { // NC non-canonical, IC canonical ("line buffered")
    TCC_VEOF,   // IC       if ICANON all bytes immediately sent to process (as if \n was entered)
    TCC_VEOL,   // IC       if ICANON another \n
    TCC_VERASE, // IC       if not ICANON works as backspace (until EOF, EOL, \n)
    TCC_VINTR,  // IC, NC   sigint
    TCC_VKILL,  // IC       if ICANON deletes entire line (until EOF, EOL, \n)
    TCC_VMIN,   // NC       minimum bytes to satisfy read for non-canonical mode
    TCC_VQUIT,  // IC, NC   sigquit
    TCC_VSUSP,  // IC, NC   sigtstp to foreground pgrp
    TCC_VTIME,  // NC       timeout value for non-canonical mode
    TCC_VSTART, // IC, NC   if flow control (not yet implemented) starts output again
    TCC_VSTOP,  // IC, NC   if flow control (not yet implemented) stops output
};

#define _POSIX_VDISABLE 0xFF // if ICANON, putting this value into control chars disables the function
static const char default_control_chars[11] = { // well imagine wanting to do ctrl+c to interrupt, look at C0 escapes, use that
    [TCC_VEOF]     = '\x04',
    [TCC_VEOL]     = '\x00',
    [TCC_VERASE]   = '\x7f',
    [TCC_VINTR]    = '\x03',
    [TCC_VKILL]    = '\x19',
    [TCC_VMIN]     = 1, // not an actual char, just a value
    [TCC_VQUIT]    = '\x1c',
    [TCC_VSUSP]    = '\x1a',
    [TCC_VTIME]    = 0, // not an actual char
    [TCC_VSTART]   = '\x11',
    [TCC_VSTOP]    = '\x13',
};

struct termios {
    char control_chars[11];
    unsigned short imodes;
    unsigned short omodes;
    unsigned short cmodes;
    unsigned short lmodes;
};

struct tty_queue { // FIFO
    char buffer[TTY_BUFFER_SIZE];
    size_t head, tail;
    //thread_queue_t write_queue; // see comments in kernel_tty_io.c
    thread_queue_t read_queue;
};


// took inspiration from the linux kernel v0.95
struct tty_t {
    char used; // here so we don't need to free() the structure
    char com_port; // -1 if not serial backed
    size_t height;
    size_t width;

    int posx, posy;

    //unsigned short * terminal_framebuffer;
    
    struct tty_queue iqueue, oqueue, tqueue; // input, output, translated, we cannot translate chars inplace

    pid_t foreground_pgrp;
    pid_t session;

    size_t (*write)(struct tty_t *);

    struct termios params;
} typedef tty_t;

extern tty_t * terminals[TTY_LIMIT_KERNEL];

void tty_alloc_kernel_console();

char tty_queue_getch(struct tty_queue * tq);
void tty_queue_putch(struct tty_queue * tq, char c);

long tty_ioctl(dev_t dev, unsigned long cmd, unsigned long arg);

long tty_write(dev_t dev, const char * s, size_t n);

#endif