#include "include/kernel_tty_io.h"
#include "include/devs.h"
#include "include/errno.h"
#include "include/fs/fs.h"
#include "include/kernel.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "include/vga.h"
#include "../libc/src/include/string.h"
#include "include/rs232.h"

#define EMPTY(tq) ((tq)->head == (tq)->tail)
#define FULL(tq) (((tq)->head == 0 && (tq)->tail == TTY_BUFFER_SIZE - 1) || (tq)->tail == (tq)->head - 1)
#define REMAIN(tq) (\
    (tq)->head < (tq)->tail ? \
        ((tq)->tail - (tq)->head) : \
        (TTY_BUFFER_SIZE - (tq)->head + (tq)->tail)\
    )
#define INC(tq) ((tq)->tail = ((tq)->tail+1)%TTY_BUFFER_SIZE)
#define DEC(tq) ((tq)->head = ((tq)->head+1)%TTY_BUFFER_SIZE)
extern size_t tty_com_write(tty_t * tty); // from rs232.c

static tty_t kernel_console = {
    .used = 1,
    .com_port = 0,
    .height = VGA_HEIGHT,
    .width = VGA_WIDTH,
    .session = 0,
    .write = tty_com_write,
    .params.lmodes = TTY_L_ECHO | TTY_L_ICANON | TTY_L_ISIG,
};

tty_t * terminals[TTY_LIMIT_KERNEL] = {
    [DEV_TTY_CONSOLE] = &kernel_console,
    //[DEV_TTY_S0] = &kernel_console,
    //[DEV_TTY_1] = &kernel_console,


};

// terminals[0] - terminals[3] = vga framebuffer backed tty devices, lctrl+rctrl+1-4

//static inline tty_t * tty_alloc(dev_t dev) {
//
//}

char tty_queue_getch(struct tty_queue * tq) {
    kassert(tq->head < TTY_BUFFER_SIZE && tq->tail < TTY_BUFFER_SIZE);
    
    while (EMPTY(tq) && !(current_process->signal & ~MASK_SIGALRM)) { // buffer empty or recieved alarm (timer)
        thread_queue_add(&tq->read_queue, current_process, current_thread, SCHED_INTERR_SLEEP);
    }

    char out = tq->buffer[tq->head];
    DEC(tq);
    //thread_queue_unblock(&tq->write_queue); // nothing happens if empty (buffer wasn't full), so no need for ifs; commented out because the circular write buffer overwrites itself
    return out;
}

void tty_queue_putch(struct tty_queue * tq, char c) {
    kassert(tq->head < TTY_BUFFER_SIZE && tq->tail < TTY_BUFFER_SIZE);

    while (FULL(tq)) { // buffer full
        kprintf("tty buffer full\n");
        DEC(tq);
        //thread_queue_add(&tq->write_queue, current_process, current_thread, SCHED_UNINTERR_SLEEP); // commented out because if buffer full, then overwrite start, needs to overwrite otherwise keyboard input would deadlock kernel
    }

    INC(tq);
    tq->buffer[tq->tail] = c;
    thread_queue_unblock(&tq->read_queue); // same as above
} 

void tty_alloc_kernel_console() { // for the kernel task, don't call for user processes
    if (kernel_task == NULL) panic("Tried to allocate console before initializing kernel task!");
    
    memcpy(kernel_console.params.control_chars, default_control_chars, sizeof(kernel_console.params.control_chars));

    spinlock_acquire(&kernel_inode_lock);
    inode_t * tty_inode = get_free_inode();
    spinlock_release(&kernel_inode_lock);

    kassert(tty_inode);

    tty_inode->device = GET_DEV(DEV_TTY, DEV_TTY_CONSOLE);
    tty_inode->is_raw_device = 1;
    tty_inode->instances = 1;


    spinlock_acquire(&kernel_fd_lock);

    kernel_task->fds[0] = kernel_task->fds[1] = kernel_task->fds[2] = get_free_fd();
    kassert(kernel_task->fds[0]);
    kernel_task->fds[0]->instances += 2;
    kernel_task->fds[0]->inode = tty_inode;

    spinlock_release(&kernel_fd_lock);
}

long tty_ioctl(dev_t dev, unsigned long cmd, unsigned long arg);

long tty_read(dev_t dev, char * s, size_t n) {
    if (MAJOR(dev) != DEV_TTY) return EINVAL;
    if (MINOR(dev) >= TTY_LIMIT_KERNEL) return EINVAL;
    if (terminals[MINOR(dev)] == NULL) return EIO;
    if (terminals[MINOR(dev)]->write == NULL) return EIO;

    while (current_process->pgrp != terminals[MINOR(dev)]->foreground_pgrp) {
        signal_process_group(current_process->pgrp, SIGTTIN);
    }

    
}

long tty_write(dev_t dev, const char * s, size_t n) {
    if (MAJOR(dev) != DEV_TTY) return EINVAL;
    if (MINOR(dev) >= TTY_LIMIT_KERNEL) return EINVAL;
    if (terminals[MINOR(dev)] == NULL) return EIO;
    if (terminals[MINOR(dev)]->write == NULL) return EIO;
    
    kassert(current_process);

    while (current_process->pgrp != terminals[MINOR(dev)]->foreground_pgrp) {
        signal_process_group(current_process->pgrp, SIGTTOU);
    }

    if (MINOR(dev) == DEV_TTY_CONSOLE) { // have to write to multiple different devices (serial, tty 1), assumes the kernel console has the serial write() function
        com_write(0, s, n); // temporary
        vga_write(s, n); // this unfortunately writes the kernel log to any currently active tty, todo: rewrite vga_write        
    }



    if (terminals[MINOR(dev)]->write != NULL)
        return terminals[MINOR(dev)]->write(terminals[MINOR(dev)]);
    else
        return n;
}
//long tty_read()