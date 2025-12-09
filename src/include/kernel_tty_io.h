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

struct tty_queue {
    char buffer[TTY_BUFFER_SIZE];
    size_t head, tail;
};


// took inspiration from the linux kernel v0.95
struct tty_t {
    char used; // here so we don't need to free() the structure
    char com_port; // -1 if not serial backed
    size_t height;
    size_t width;

    int posx, posy;

    //unsigned short * terminal_framebuffer;
    
    struct tty_queue iqueue, oqueue;

    pid_t foreground_pgrp;
    pid_t session;

    size_t (*write)(struct tty_t *);

    unsigned short imodes;
    unsigned short omodes;
    unsigned short cmodes;
    unsigned short lmodes;
} typedef tty_t;

extern tty_t * terminals[TTY_LIMIT_KERNEL];

long tty_ioctl(dev_t dev, unsigned long cmd, unsigned long arg);

#endif