#ifndef KERNEL_TTY_IO_H
#define KERNEL_TTY_IO_H
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include "kernel_sched_queues.h"
#include <limits.h>
#include <UnstableOS/devs.h>
#include <termios.h>
#include <sys/types.h>

#define TTY_LONGEST_BREAK_MSEC 2500
#define KERNEL_CONSOLE_MINOR DEV_TTY_0
#define TTY_TAB_WIDTH 8 // don't increase past 16, otherwise OOB read in fbconsole
#define TTYDEF_IFLAG    (ICRNL | IXANY | IXON)
#define TTYDEF_OFLAG    (OPOST | ONLCR)
#define TTYDEF_LFLAG    (ECHO | ECHOE | ECHOK | ICANON | ISIG | ECHOCTL)
#define TTYDEF_CFLAG    (B115200 | CS8 | CREAD | HUPCL)

#define EMPTY(tq) ((tq)->head == (tq)->tail)
#define FULL(tq) (((tq)->head == 0 && (tq)->tail == MAX_CANON - 1) || (tq)->tail == (tq)->head - 1)
#define REMAIN(tq) (\
    (tq)->head <= (tq)->tail ? \
        ((tq)->tail - (tq)->head) : \
        (MAX_CANON - (tq)->head + (tq)->tail)\
    )// how many elements still in queue
#define INC(tq) ((tq)->tail = ((tq)->tail+1)%MAX_CANON) // lenghtens queue
#define DEC(tq) ((tq)->head = ((tq)->head+1)%MAX_CANON) // shortens queue by removing oldest element
#define DEC_LAST(tq) ((tq)->tail = (MAX_CANON + (tq)->tail-1)%MAX_CANON) // shortens queue by removing youngest element (for VERASE, VKILL)

#define MAX_CANON 4096

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


struct tty_queue {
    spinlock_t queue_lock;
    char buffer[MAX_CANON];
    size_t head, tail; // tail = pointer to the next free char
    thread_queue_t read_queue;

    thread_queue_t ix_queue;

    // useful for the input queue
    unsigned long tty_start_column; // where this buffer started at, for tabs
};


// took inspiration from the linux kernel v0.95
struct tty_t {
    // here so we don't need to free()/close() this terminal
    // for aliased ttys like console
    unsigned long instances;
    union {
        struct tty_t * pty_buddy;
        int com_port;
    };
    spinlock_t tty_lock; // for params
    size_t height;
    size_t width;

    unsigned long tty_column; // for ONOCR
    struct tty_queue iqueue, oqueue; // input, output

    pid_t foreground_pgrp;
    pid_t session; // 0 assumes not taken

    // for pseudoterminals to let devfs adjust the inode owner
    // TODO: this could be done better,
    //       this way it's stuck on the original UID at the time of the ptmx open
    //       shouldn't hopefully matter too much, but still
    uid_t owner;

    size_t (*write)(struct tty_t *);

    // following function are also required to set hang_up and wake up poll queues where applicable
    // notably, hup to set to 1, and ctl to set based on whether cfgetospeed(tio) == B0
    // optional for setting control flags and similar,
    // implementations should edit the termios structure's based on what got applied
    int    (*ctl)  (struct tty_t *, struct termios *);
    // optional for break conditions, on/off
    void   (*brk)  (struct tty_t *, int);
    // optional for hangups on file close (cfsetospeed B0 is handled by ctl)
    void   (*hup)  (struct tty_t *);
    size_t read_remaining;

    struct thread_queue pollin, pollout;

    struct termios params;
    char input_stopped, output_stopped; // see IXON, IXOFF
    char hung_up; // needed for poll
} typedef tty_t;

extern tty_t * terminals[TTY_LIMIT_KERNEL];

tty_t * tty_init_tty(tcflag_t imodes, tcflag_t lmodes, tcflag_t omodes, tcflag_t
                     cmodes,
                     const unsigned char * control_chars, size_t height,
                     size_t width, size_t (*write)(tty_t *),
                     int (*ctl)(struct tty_t*, struct termios*), void (*brk)(struct tty_t*, int), void
                     (*hup)(struct tty_t*), int com_port, pid_t controlling_session, pid_t foreground_pgrp);
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
int tty_queue_putch(tty_t * tty, struct tty_queue * tq, char c, char onlret);

ssize_t tty_pwrite(file_descriptor_t * file, const void * s, size_t n, off_t offset);
ssize_t tty_pread(file_descriptor_t * file, void * s, size_t n, off_t offset);
long tty_ioctl(file_descriptor_t * file, unsigned long request, void * arg);

long tty_write_to_tty(const char * s, size_t n, dev_t dev, char parmarked);

// for hardware drivers to call to send these conditions to foreground groups
long tty_recv_break(dev_t dev);
long tty_recv_hang_up(dev_t dev);
#endif