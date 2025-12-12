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
    .params.omodes = TTY_O_POST | TTY_O_NLCR,
    .params.imodes = TTY_O_CRNL
};

void tty_alloc_kernel_console() { // for the kernel task, don't call for user processes
    if (kernel_task == NULL) panic("Tried to allocate console before initializing kernel task!");
    
    memcpy(kernel_console.params.control_chars, default_control_chars, sizeof(kernel_console.params.control_chars));

    spinlock_acquire(&kernel_inode_lock);
    inode_t * tty_inode = get_free_inode();
    spinlock_release(&kernel_inode_lock);

    kassert(tty_inode);

    tty_inode->device = GET_DEV(DEV_MAJ_TTY_META, DEV_TTY_CONSOLE);
    tty_inode->is_raw_device = 1;
    tty_inode->instances = 1;


    spinlock_acquire(&kernel_fd_lock);

    kernel_task->fds[0] = kernel_task->fds[1] = kernel_task->fds[2] = get_free_fd();
    kassert(kernel_task->fds[0]);
    kernel_task->fds[0]->instances += 2;
    kernel_task->fds[0]->inode = tty_inode;

    spinlock_release(&kernel_fd_lock);
}

tty_t * terminals[TTY_LIMIT_KERNEL] = {
    [DEV_TTY_S0] = &kernel_console,
    [DEV_TTY_0] = &kernel_console,
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

#define TTY_QUEUE_MODE 0 // 0 = discards new input, 1 = overwrites old input, 2 = blocks until writable

void tty_queue_putch(struct tty_queue * tq, char c) {
    kassert(tq->head < TTY_BUFFER_SIZE && tq->tail < TTY_BUFFER_SIZE);

    while (FULL(tq)) { // buffer full
        kprintf("tty buffer full\n");
        #if TTY_QUEUE_MODE == 0
            return; // discards new data
        #elif TTY_QUEUE_MODE == 1
            DEC(tq); // rewrites old data
        #elif TTY_QUEUE_MODE == 2
            thread_queue_add(&tq->write_queue, current_process, current_thread, SCHED_UNINTERR_SLEEP); // warning! keyboard input can deadlock kernel
        #endif
    }

    INC(tq);
    tq->buffer[tq->tail] = c;
    thread_queue_unblock(&tq->read_queue); // same as above
} 


long tty_ioctl(dev_t dev, unsigned long cmd, unsigned long arg);

long tty_read(dev_t dev, char * s, size_t n) {
    if (MAJOR(dev) != DEV_MAJ_TTY && MAJOR(dev) != DEV_MAJ_TTY_META) return EINVAL;

    if (MAJOR(dev) == DEV_MAJ_TTY) {
        if (MINOR(dev) >= TTY_LIMIT_KERNEL) return EINVAL;
        
        if (terminals[MINOR(dev)] == NULL) return EIO;
        if (terminals[MINOR(dev)]->write == NULL) return EIO;
    } 
    
    kassert(current_process);

    if (MAJOR(dev) == DEV_MAJ_TTY_META) {
        switch (MINOR(dev)) {
            case DEV_TTY_CONSOLE:
            case DEV_TTY_CURRENT:
            default:
                kprintf("reading with any meta tty not yet implemented\n");
                return EINVAL;
        }
        return EINVAL; // just in case
    } else {
        while (current_process->pgrp != terminals[MINOR(dev)]->foreground_pgrp) {
            signal_process_group(current_process->pgrp, SIGTTIN);
        }
    }

    return EIO;
}

long tty_write(dev_t dev, const char * s, size_t n) {
    if (MAJOR(dev) != DEV_MAJ_TTY && MAJOR(dev) != DEV_MAJ_TTY_META) return EINVAL;

    if (MAJOR(dev) == DEV_MAJ_TTY) {
        if (MINOR(dev) >= TTY_LIMIT_KERNEL) return EINVAL;
        
        if (terminals[MINOR(dev)] == NULL) return EIO;
        if (terminals[MINOR(dev)]->write == NULL) return EIO;
    } 
    
    kassert(current_process);

    if (MAJOR(dev) == DEV_MAJ_TTY_META) {
        switch (MINOR(dev)) {
            case DEV_TTY_CONSOLE:
                com_write(0, s, n); // temporary
                vga_write(s, n); // this unfortunately writes the kernel log over any currently active vga backed tty, todo: rewrite vga_write and or this entire thing
                return n;
            case DEV_TTY_CURRENT:
                // TODO: get controlling terminal inode from current processes session
                kprintf("interactions with DEV_TTY_CURRENT not yet implemented\n");
            default:
                return EINVAL;
        }
        return EINVAL; // just in case
    } else {
        while (current_process->pgrp != terminals[MINOR(dev)]->foreground_pgrp) {
            signal_process_group(current_process->pgrp, SIGTTOU);
        }
    }

    kassert(MAJOR(dev != DEV_MAJ_TTY_META));



    if (terminals[MINOR(dev)]->write != NULL)
        return terminals[MINOR(dev)]->write(terminals[MINOR(dev)]);
    else
        return n;
}
long tty_write_to_tty(const char * s, size_t n, dev_t dev) { // writes data into read queue of a tty, aka recv input
    if (MAJOR(dev) != DEV_MAJ_TTY && MAJOR(dev) != DEV_MAJ_TTY_META) return EINVAL;

    if (MAJOR(dev) == DEV_MAJ_TTY) {
        if (MINOR(dev) >= TTY_LIMIT_KERNEL) return EINVAL;
        
        if (terminals[MINOR(dev)] == NULL) return EIO;
        if (terminals[MINOR(dev)]->write == NULL) return EIO;
    } 
    
    if (MAJOR(dev) == DEV_MAJ_TTY_META) {
        switch (MINOR(dev)) {
            case DEV_TTY_CONSOLE: // assuming dev_tty_console is both ttyS0 and tty0, tty0 because that has a visual representation, todo: fix
                return tty_write_to_tty(s, n, GET_DEV(DEV_MAJ_TTY, DEV_TTY_0));
            case DEV_TTY_CURRENT:
                kprintf("interactions with DEV_TTY_CURRENT not yet implemented\n");
            default:
                return EINVAL;
        }
        return EINVAL; // just in case
    }

    kassert(MAJOR(dev != DEV_MAJ_TTY_META));

    for (int i = 0; i < n; i++) {
        tty_queue_putch(&terminals[MINOR(dev)]->iqueue, s[i]);
    }

    if ((MINOR(dev) >= DEV_TTY_0 && MINOR(dev) < DEV_TTY_0 + __TTY_CONSOLE) || MINOR(dev) == DEV_TTY_S0) { 
            // serial and ptys don't have a visual representations and don't need to be updated, 
            // exception is DEV_TTY_S0, because thats the kernel console (also tty0)
        if (terminals[MINOR(dev)]->params.lmodes & TTY_L_ECHO) vga_write(s, n); // TODO: rewrite
    }

    return n;
}