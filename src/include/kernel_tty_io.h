#ifndef KERNEL_TTY_IO_H
#define KERNEL_TTY_IO_H
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include "kernel_ioctl.h"
#include "devs.h"

#define EMPTY(tq) ((tq)->head == (tq)->tail)
#define FULL(tq) (((tq)->head == 0 && (tq)->tail == TTY_BUFFER_SIZE - 1) || (tq)->tail == (tq)->head - 1)
#define REMAIN(tq) (\
    (tq)->head <= (tq)->tail ? \
        ((tq)->tail - (tq)->head) : \
        (TTY_BUFFER_SIZE - (tq)->head + (tq)->tail)\
    )// how many elements still in queue
#define INC(tq) ((tq)->tail = ((tq)->tail+1)%TTY_BUFFER_SIZE) // lenghtens queue
#define DEC(tq) ((tq)->head = ((tq)->head+1)%TTY_BUFFER_SIZE) // shortens queue by removing oldest element
#define DEC_LAST(tq) ((tq)->tail = (TTY_BUFFER_SIZE + (tq)->tail-1)%TTY_BUFFER_SIZE) // shortens queue by removing youngest element (for VERASE, VKILL)

#define TTY_BUFFER_SIZE 4096
#define MAX_CANNON TTY_BUFFER_SIZE

#define TTY_L_ECHO 1
#define TTY_L_ICANON 2 // new line buffering
#define TTY_L_ISIG 4 // enable signals
#define TTY_L_TOSTOP 8 // set SIGTTOU if background process group tries to write()

// as you might have noticed, i pick from the posix standard based on how easy things are to implement
#define TTY_I_CRNL 1 // carriage return -> new line
#define TTY_I_IGNCR 2 // ignore carriage return
#define TTY_I_NLCR 4 // new line -> carriage return
#define TTY_I_STRIP 8 // strip 8 bit ascii to 7 bit
#define TTY_I_BRKINT 16 // VINTR sends sigint
#define TTY_I_IGNBRK 32 // ignoring VINTR, trainslating into NULL byte
//#define TTY_I_XOFF 64 // we can send VSTOP/VSTART to pause/resume the transmit of the other side TODO: implement
#define TTY_I_XON 128 // recieving TCC_VSTOP pauses output, recieving TCC_VSTART resumes output
#define TTY_I_XANY 256 // any recieved character resumes output

#define TTY_O_POST 1 // whether to even do processing
#define TTY_O_NLCR 2 // new line -> carriage return new line
#define TTY_O_CRNL 4 // carriage return -> new line
#define TTY_O_NLRET 8 // new line does also carriage return


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
    TCC_VSTART, // IC, NC   if flow control starts output again
    TCC_VSTOP,  // IC, NC   if flow control stops output
};

#define _POSIX_VDISABLE 0xFF // if ICANON, putting this value into control chars disables the function
static const unsigned char default_control_chars[11] = { // well imagine wanting to do ctrl+c to interrupt, look at C0 escapes, use that
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

typedef unsigned short tcflag_t;
struct termios {
    unsigned char control_chars[11];
    tcflag_t imodes;
    tcflag_t omodes;
    tcflag_t cmodes;
    tcflag_t lmodes;

    char input_stopped, output_stopped; // see IXON, IXOFF
};

struct tty_queue {
    spinlock_t queue_lock;
    char buffer[TTY_BUFFER_SIZE];
    size_t head, tail; // tail = pointer to the next free char
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
    
    struct tty_queue iqueue, oqueue; // input, output

    pid_t foreground_pgrp;
    pid_t session;

    size_t (*write)(struct tty_t *);

    size_t read_remaining;

    struct termios params;
} typedef tty_t;

extern tty_t * terminals[TTY_LIMIT_KERNEL];

void tty_alloc_kernel_console();

int tty_queue_getch(struct tty_queue * tq); // if 256, recieved SIGALRM
void tty_queue_putch(struct tty_queue * tq, char c);

long tty_ioctl(dev_t dev, unsigned long cmd, unsigned long arg);

ssize_t tty_write(dev_t dev, const char * s, size_t n);
ssize_t tty_read(dev_t dev, char * s, size_t n);

long tty_write_to_tty(const char * s, size_t n, dev_t dev);

#endif