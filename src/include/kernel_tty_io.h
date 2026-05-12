#ifndef KERNEL_TTY_IO_H
#define KERNEL_TTY_IO_H
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include <UnstableOS/devs.h>
#include <termios.h>

#define TTYDEF_IFLAG    (ICRNL | ISTRIP | IXANY | IXON)
#define TTYDEF_OFLAG    (OPOST | ONLCR)
#define TTYDEF_LFLAG    (ECHO | ECHOE | ECHOK | ICANON | ISIG | ECHOCTL)

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

#define CTRL(c) ((c) & 0x1F)
static const unsigned char default_control_chars[11] = { // well imagine wanting to do ctrl+c to interrupt, look at C0 escapes, use that
    [VEOF]   = CTRL('D'),
    [VEOL]   = '\x00',
    [VERASE] = '\x7f',
    [VINTR]  = CTRL('C'),
    [VKILL]  = CTRL('U'),
    [VMIN]   = 1, // minimum amount of data needed to flush the tty
    [VQUIT]  = CTRL('\\'),
    [VSTART] = CTRL('Q'),
    [VSTOP]  = CTRL('S'),
    [VSUSP]  = CTRL('Z'),
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

// specify 0 for no timeout -> wait forever
// specify -1 in timespec->tv_nsec to return on empty buffer
// otherwise blocks until timeout expires
// this way to facilitate all 4 modes of POSIX terminal reading
// cannot just return 256, because signals always return from read()
// returns 257 on timer expiry, or on empty buffer if timespec->tv_nsec is -1
// returns 256 on signal
int tty_queue_getch(struct tty_queue * tq, struct timespec timeout);

// onlret being the same as the termios flag, that is resetting tty column to 0 on \n, \v
int tty_queue_putch(struct tty_queue * tq, char c, char onlret);

ssize_t tty_write(file_descriptor_t * file, const void * s, size_t n);
ssize_t tty_read(file_descriptor_t * file, void * s, size_t n);
long tty_ioctl(file_descriptor_t * file, unsigned long request, void * arg);

long tty_write_to_tty(const char * s, size_t n, dev_t dev);

#endif