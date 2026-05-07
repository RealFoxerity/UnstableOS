#ifndef KERNEL_TTY_IO_H
#define KERNEL_TTY_IO_H
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include "../../libc/src/include/UnstableOS/devs.h"
#include <termios.h>

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

#define _POSIX_VDISABLE 0xFF // if ICANON, putting this value into control chars disables the function
static const unsigned char default_control_chars[11] = { // well imagine wanting to do ctrl+c to interrupt, look at C0 escapes, use that
    [VEOF]   = 'D' - '@',
    [VEOL]   = '\x00',
    [VERASE] = '\x7f',
    [VINTR]  = 'C' - '@',
    [VKILL]  = 'U' - '@',
    [VMIN]   = 1, // minimum amount of data needed to flush the tty
    [VQUIT]  = '\\' - '@',
    [VSTART] = 'Q' - '@',
    [VSTOP]  = 'S' - '@',
    [VSUSP]  = 'Z' - '@',
    [VTIME]  = 0, // not an actual char, timeout in deciseconds for noncanonical read, not implemented
};


#define TTY_QUEUE_MODE 0 // 0 = discards new input, 1 = overwrites old input, 2 = blocks until writable

struct tty_queue {
    spinlock_t queue_lock;
    char buffer[TTY_BUFFER_SIZE];
    size_t head, tail; // tail = pointer to the next free char
#if TTY_QUEUE_MODE == 2
    thread_queue_t write_queue;
#endif
    thread_queue_t read_queue;

    thread_queue_t ix_queue;

    unsigned long tty_column; // for ONOCR
};


// took inspiration from the linux kernel v0.95
struct tty_t {
    char used; // here so we don't need to free() the structure
    char com_port; // -1 if not serial backed
    spinlock_t tty_lock; // for params
    size_t height;
    size_t width;

    struct tty_queue iqueue, oqueue; // input, output

    pid_t foreground_pgrp;
    pid_t session;

    size_t (*write)(struct tty_t *);

    size_t read_remaining;

    struct termios params;
    char input_stopped, output_stopped; // see IXON, IXOFF
} typedef tty_t;

extern tty_t * terminals[TTY_LIMIT_KERNEL];

tty_t * tty_init_tty(tcflag_t imodes, tcflag_t lmodes, tcflag_t omodes, const unsigned char * control_chars,
                    size_t height, size_t width,
                    size_t (*write)(struct tty_t *), char com_port,
                    pid_t controlling_session, pid_t foreground_pgrp);
void tty_register(tty_t * tty, dev_t minor);

void tty_alloc_kernel_console();

int tty_queue_getch(struct tty_queue * tq); // if 256, recieved SIGALRM

// onlret being the same as the termios flag, that is resetting tty column to 0 on \n, \v
int tty_queue_putch(struct tty_queue * tq, char c, char onlret);

ssize_t tty_write(file_descriptor_t * file, const void * s, size_t n);
ssize_t tty_read(file_descriptor_t * file, void * s, size_t n);
long tty_ioctl(file_descriptor_t * file, unsigned long request, void * arg);

long tty_write_to_tty(const char * s, size_t n, dev_t dev);

#endif