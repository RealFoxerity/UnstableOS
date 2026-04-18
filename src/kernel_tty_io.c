#include "kernel_tty_io.h"
#include "devs.h"
#include "dev_ops.h"
#include <errno.h>
#include "fs/fs.h"
#include "kernel.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include "gfx.h"
#include "kernel_console.h"
#include <string.h>

spinlock_t tty_lock = {0};
tty_t * terminals[TTY_LIMIT_KERNEL] = {0};

static struct dev_operations tty_ops = {
    .read = tty_read,
    .write = tty_write,
    .ioctl = tty_ioctl,
};

long tty_ioctl(file_descriptor_t * file, unsigned long command, void * arg) {

}

// terminals[0] - terminals[3] = vga framebuffer backed tty devices, lctrl+rctrl+1-4


// TODO: seems like i don't do VERASE and VKILL properly

extern size_t tty_com_write(tty_t * tty); // from rs232.c

static size_t tty_console_write(tty_t * tty) {
    struct tty_queue * tq = &tty->oqueue;
    spinlock_acquire_interruptible(&tq->queue_lock);

    size_t n = REMAIN(tq);

    if (tq->head <= tq->tail) {
        console_write(tq->buffer + tq->head, n);
    } else {
        console_write(tq->buffer + tq->head, TTY_BUFFER_SIZE - tq->head);
        console_write(tq->buffer, tq->tail);
    }

    tty_com_write(tty);
    tty->oqueue.head = tty->oqueue.tail = 0;

    spinlock_release(&tq->queue_lock);
    return n;
}

tty_t * tty_init_tty(tcflag_t imodes, tcflag_t lmodes, tcflag_t omodes, const unsigned char * control_chars,
                    size_t height, size_t width,
                    size_t (*write)(struct tty_t *), char com_port,
                    pid_t controlling_session, pid_t foreground_pgrp) {
    tty_t * new_tty = kalloc(sizeof(tty_t));
    if (!new_tty) return NULL;
    memset(new_tty, 0, sizeof(tty_t));

    *new_tty = (tty_t) {
        .used = 1,
        .foreground_pgrp = foreground_pgrp,
        .height = height,
        .width = width,
        .session = controlling_session,
        .write = write,
        .params.imodes = imodes,
        .params.lmodes = lmodes,
        .params.omodes = omodes,
    };
    memcpy(new_tty->params.control_chars, control_chars, sizeof(new_tty->params.control_chars));

    return new_tty;
}

void tty_register(tty_t * tty, dev_t minor) {
    kassert(tty);
    kassert(minor < TTY_LIMIT_KERNEL);
    spinlock_acquire(&tty_lock);
    kassert(!terminals[minor]);
    terminals[minor] = tty;
    dev_register_ops(GET_DEV(DEV_MAJ_TTY, minor), &tty_ops);
    spinlock_release(&tty_lock);
}

void tty_alloc_kernel_console() { // for the kernel task, don't call for user processes
    if (kernel_task == NULL) panic("Tried to allocate console before initializing kernel task!");

    tty_t * kernel_console = tty_init_tty(
        TTY_I_CRNL,
        TTY_L_ECHO | TTY_L_ECHOE | TTY_L_ECHOK | TTY_L_ECHOCTL | TTY_L_ICANON | TTY_L_ISIG,
        TTY_O_POST | TTY_O_NLCR,
        default_control_chars,
        display_height, display_width,
        tty_console_write, 0,
        0, 0);
    tty_register(kernel_console, DEV_TTY_0);
    tty_register(kernel_console, DEV_TTY_S0);

    dev_register_ops(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE), &tty_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CURRENT), &tty_ops);
}

int tty_queue_getch(struct tty_queue * tq) { // if 256, got SIGALRM
    kassert(tq->head < TTY_BUFFER_SIZE && tq->tail < TTY_BUFFER_SIZE);

    again:
    if (EMPTY(tq))
        thread_queue_add(&tq->read_queue, current_process, current_thread, SCHED_INTERR_SLEEP);
    if (current_thread->sa_to_be_handled)
        return 256; // any signal interrupting

    char out = 0;
    spinlock_acquire_interruptible(&tq->queue_lock);
    if (EMPTY(tq)) {
        spinlock_release(&tq->queue_lock);
        goto again;
    }

    out = tq->buffer[tq->head]; // see comment in kernel_tty_io.h for queues
    DEC(tq);
    //thread_queue_unblock(&tq->write_queue); // nothing happens if empty (buffer wasn't full), so no need for ifs; commented out because the circular write buffer overwrites itself
    spinlock_release(&tq->queue_lock);
    return out;
}

#define TTY_QUEUE_MODE 0 // 0 = discards new input, 1 = overwrites old input, 2 = blocks until writable

int tty_queue_putch(struct tty_queue * tq, char c) {
    kassert(tq->head < TTY_BUFFER_SIZE && tq->tail < TTY_BUFFER_SIZE);
    spinlock_acquire_interruptible(&tq->queue_lock);
    while (FULL(tq)) { // buffer full
        kprintf("tty buffer full, flushing\n");
        thread_queue_unblock(&tq->read_queue);
        if (current_thread->sa_to_be_handled) {
            spinlock_release(&tq->queue_lock);
            return 256;
        }
        #if TTY_QUEUE_MODE == 0
            spinlock_release(&tq->queue_lock);
            return 0; // discards new data
        #elif TTY_QUEUE_MODE == 1
            DEC(tq); // rewrites old data
        #elif TTY_QUEUE_MODE == 2
            spinlock_release(&tq->queue_lock);
            thread_queue_add(&tq->write_queue, current_process, current_thread, SCHED_UNINTERR_SLEEP); // warning! keyboard input can deadlock kernel
            spinlock_acquire_interruptible(&tq->queue_lock);
        #endif
    }

    tq->buffer[tq->tail] = c;
    INC(tq);
    //thread_queue_unblock(&tq->read_queue); // commented out, because flushing is handled by the icannon flag
    spinlock_release(&tq->queue_lock);
    return 0;
}

void tty_flush_input(tty_t * tty) { // flush for reading on new line or EOF in case of canonical/line buffered mode
    thread_queue_unblock(&tty->iqueue.read_queue);
}

//void tty_flush_output(tty_t * tty) {
//    thread_queue_unblock(&tty->oqueue.read_queue);
//}


static size_t tty_translate_line_outgoing(const char * s, size_t n, tty_t * tty);

static inline char tty_remove_char(tty_t * tty, char is_vkill) { // cannon mode, ERASE char, returns 1 when actually removed a char
    spinlock_acquire_interruptible(&tty->iqueue.queue_lock);
    if (REMAIN(&tty->iqueue) > 0) {
        if (!(
            tty->iqueue.buffer[tty->iqueue.tail-1] == '\n' ||
            (tty->params.control_chars[TCC_VEOF] != _POSIX_VDISABLE && tty->iqueue.buffer[tty->iqueue.tail-1] == tty->params.control_chars[TCC_VEOF]) ||
            (tty->params.control_chars[TCC_VEOL] != _POSIX_VDISABLE && tty->iqueue.buffer[tty->iqueue.tail-1] == tty->params.control_chars[TCC_VEOL])
        )) {
            if (!is_vkill && tty->params.lmodes & TTY_L_ECHO) {
                if (tty->params.lmodes & TTY_L_ECHOE) {
                    if (tty_translate_line_outgoing("\b \b", 3, tty) != 3) {
                        spinlock_release(&tty->iqueue.queue_lock);
                        return -1;
                    }
                    switch (tty->iqueue.buffer[tty->iqueue.tail-1]) {
                        case '\a':
                        case '\b':
                        case '\t':
                        case '\n':
                        case '\v':
                        case '\f':
                        case '\r':
                            break;
                        default:
                            if (tty->params.lmodes & TTY_L_ECHOCTL) {
                                if (tty->iqueue.buffer[tty->iqueue.tail-1] < ' ' ||

                                    (tty->params.control_chars[TCC_VERASE] != _POSIX_VDISABLE &&
                                    tty->iqueue.buffer[tty->iqueue.tail-1] == tty->params.control_chars[TCC_VERASE])) // probably not gonna happen
                                {
                                    // remove the ^ from ^X escape
                                    if (tty_translate_line_outgoing("\b \b", 3, tty) != 3) {
                                        spinlock_release(&tty->iqueue.queue_lock);
                                        return -1;
                                    }
                                }
                            }
                    }
                }
                else {
                    if (tty_translate_line_outgoing((const char *)&tty->params.control_chars[TCC_VERASE], 1, tty) != 1) {
                        spinlock_release(&tty->iqueue.queue_lock);
                        return -1;
                    }
                }
            }
            DEC_LAST(&tty->iqueue);
            spinlock_release(&tty->iqueue.queue_lock);
            return 1;
        }
    }
    spinlock_release(&tty->iqueue.queue_lock);
    return 0;
}

static inline char tty_remove_line(tty_t * tty) { // cannon mode, KILL char
    while (tty_remove_char(tty, 1));
    if (tty->params.lmodes & TTY_L_ECHO) {
        if (tty_translate_line_outgoing((const char *)&tty->params.control_chars[TCC_VKILL], 1, tty) != 1) return -1;
        if (tty->params.lmodes & TTY_L_ECHOK)
            if (tty_translate_line_outgoing("\n", 1, tty) != 1) return -1;
    }
    return 0;
}

// TODO: check ordering of operations
static inline size_t tty_translate_line_incoming(const char * s, size_t n, tty_t * tty) {
    kassert(s);
    kassert(tty);

    char checked;
    for (size_t i = 0; i < n; i++) {
        checked = s[i];

        if (tty->params.imodes & TTY_I_XON) {
            if (tty->params.control_chars[TCC_VSTOP] != _POSIX_VDISABLE && checked == tty->params.control_chars[TCC_VSTOP]) {
                tty->params.input_stopped = 1;
                continue;
            }
            if (tty->params.control_chars[TCC_VSTART] != _POSIX_VDISABLE && checked == tty->params.control_chars[TCC_VSTART]) {
                tty->params.input_stopped = 0;
                continue;
            }
            if (tty->params.imodes & TTY_I_XANY) tty->params.input_stopped = 0;
            if (tty->params.input_stopped) continue;
        }

        if (tty->params.lmodes & TTY_L_ICANON) {
            if (tty->params.control_chars[TCC_VERASE] != _POSIX_VDISABLE && checked == tty->params.control_chars[TCC_VERASE]) {
                if (tty_remove_char(tty, 0) == -1) return i;
                continue;
            }
            if (tty->params.control_chars[TCC_VKILL] != _POSIX_VDISABLE && checked == tty->params.control_chars[TCC_VKILL]) {
                if (tty_remove_line(tty) == -1) return i;
                continue;
            }
        }

        if (tty->params.imodes & TTY_I_STRIP) checked &= 0x7F; // stripping top bit
        char final = checked;
        char skip_char = 0;
        switch (checked) {
            case '\r':
                if (tty->params.imodes & TTY_I_IGNCR) break;
                if (tty->params.imodes & TTY_I_CRNL) {
                    final = '\n';
                    break;
                }
            case '\n':
                if (tty->params.imodes & TTY_I_NLCR) {
                    final = '\r';
                    break;
                }
            default:
                if (tty->params.lmodes & TTY_L_ISIG) {
                    if (tty->params.control_chars[TCC_VINTR] != _POSIX_VDISABLE && checked == tty->params.control_chars[TCC_VINTR]) {
                        signal_process_group(tty->foreground_pgrp, &(siginfo_t) {.si_signo = SIGINT });
                        tty_flush_input(tty);
                        return i;
                    } else if (tty->params.control_chars[TCC_VQUIT] != _POSIX_VDISABLE && checked == tty->params.control_chars[TCC_VQUIT]) {
                        signal_process_group(tty->foreground_pgrp, &(siginfo_t) {.si_signo = SIGQUIT });
                        tty_flush_input(tty);
                        return i;
                    } else if (tty->params.control_chars[TCC_VSUSP] != _POSIX_VDISABLE && checked == tty->params.control_chars[TCC_VSUSP]) {
                        signal_process_group(tty->foreground_pgrp, &(siginfo_t) {.si_signo = SIGTSTP });
                        tty_flush_input(tty);
                        return i;
                    }
                }
        }
        if (skip_char) continue;

        if (tty_queue_putch(&tty->iqueue, final) == 256) return i;
        if (tty->params.lmodes & TTY_L_ECHO) {
            if (tty->params.lmodes & TTY_L_ECHOCTL && final < ' ') {
                if ((tty->params.control_chars[TCC_VSTART] != _POSIX_VDISABLE &&
                    final == tty->params.control_chars[TCC_VSTART]) ||

                    (tty->params.control_chars[TCC_VSTOP] != _POSIX_VDISABLE &&
                    final == tty->params.control_chars[TCC_VSTOP]) ||

                    final == '\n' || final == '\t')
                {
                    if (tty_translate_line_outgoing(&final, 1, tty) != 1) return i;
                } else {
                    if (tty_translate_line_outgoing("^", 1, tty) != 1) return i;
                    if (tty_translate_line_outgoing(&(char){final + '@'}, 1, tty) != 1) return i;
                }
            } else if (tty->params.lmodes & TTY_L_ECHOCTL &&
                    !(tty->params.lmodes & TTY_L_ICANON) &&
                    final == 0x7f)
            {
                if (tty_translate_line_outgoing("^?", 2, tty) != 2) return i;
            } else {
                if (tty_translate_line_outgoing(&final, 1, tty) != 1) return i;
            }
        }

        if (tty->params.lmodes & TTY_L_ICANON &&
                (final == '\n' ||
                (tty->params.control_chars[TCC_VEOF] != _POSIX_VDISABLE && final == tty->params.control_chars[TCC_VEOF]) ||
                (tty->params.control_chars[TCC_VEOL] != _POSIX_VDISABLE && final == tty->params.control_chars[TCC_VEOL]))) {
            tty_flush_input(tty);
        } else if (!(tty->params.lmodes & TTY_L_ICANON) &&
                    ((tty->params.control_chars[TCC_VMIN] != _POSIX_VDISABLE &&
                        REMAIN(&tty->iqueue) >= tty->params.control_chars[TCC_VMIN]) ||
                        tty->params.control_chars[TCC_VMIN] == _POSIX_VDISABLE)
        ){
            tty_flush_input(tty);
        }
    }
    return n;
}

static size_t tty_translate_line_outgoing(const char * s, size_t n, tty_t * tty) {
    kassert(s);
    kassert(tty);

    if (tty->write == NULL) return n; // useless to write to a nonbacked tty

    if (!(tty->params.omodes & TTY_O_POST)) { // to avoid useless switch
        for (size_t i = 0; i < n; i++) if (tty_queue_putch(&tty->oqueue, s[i]) == 256) return i;
        return n;
    }

    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
            case '\r':
                if (tty->params.omodes & TTY_O_CRNL) {
                    if (tty_queue_putch(&tty->oqueue, '\n') == 256) return i;
                    break;
                }
            case '\n':
                if (tty->params.omodes & TTY_O_NLCR) {
                    if (!(tty->params.omodes & TTY_O_CRNL)) {if (tty_queue_putch(&tty->oqueue, '\r') == 256) return i;}
                    else {if (tty_queue_putch(&tty->oqueue, '\n') == 256) return i;} // \n twice? TODO: implement ONOCR, requires tracking current position
                    if (tty_queue_putch(&tty->oqueue, '\n') == 256) return i;
                    break;
                }
            default:
                if (tty_queue_putch(&tty->oqueue, s[i]) == 256) return i;
        }
        //if (tty->params.lmodes & TTY_L_ICANON &&
        //        (s[i] == '\n' ||
        //        (tty->params.control_chars[TCC_VEOF] != _POSIX_VDISABLE && s[i] == tty->params.control_chars[TCC_VEOF]) ||
        //        (tty->params.control_chars[TCC_VEOL] != _POSIX_VDISABLE && s[i] == tty->params.control_chars[TCC_VEOL]))) {
        //    tty->write(tty);
        //}
        //if (FULL(&tty->oqueue) || !(tty->params.lmodes & TTY_L_ICANON)) tty->write(tty);
        tty->write(tty);
    }

    return n;
}

static inline char is_valid_tty(dev_t dev) { // checks if the device is a valid raw tty - an actual tty object, so no meta ttys
    if (MAJOR(dev) != DEV_MAJ_TTY) return 0;
    if (MINOR(dev) >= TTY_LIMIT_KERNEL) return 0;
    if (terminals[MINOR(dev)] == NULL) return 0;
    if (terminals[MINOR(dev)]->write == NULL) return 0;

    return 1;
}

ssize_t tty_read(file_descriptor_t * file, void * s, size_t n) {
    // assuming now file is a valid pointer
    dev_t dev = file->inode->device;
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE)) dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_S0); // kernel console can only read (which shouldn't happen anyway) from first serial
    if (!is_valid_tty(dev)) return -EINVAL;

    if (n == 0) return 0;

    kassert(current_process);

    tty_t * tty = terminals[MINOR(dev)];

    if (current_process->pgrp != tty->foreground_pgrp) {
        signal_process_group(current_process->pgrp, &(siginfo_t) {.si_signo = SIGTTIN });
        return -EINTR;
    }

    if (tty->read_remaining != 0) return -EAGAIN; // something else is already reading

    while (!__atomic_compare_exchange_n(&tty->read_remaining, &(unsigned long){0}, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) asm volatile ("pause");

    int out = 0;
    for (char * i = s; i < (char*)s + n; i++) {
        out = tty_queue_getch(&tty->iqueue);
        if (tty->params.lmodes & TTY_L_ICANON) {
            if ((tty->params.control_chars[TCC_VEOF] != _POSIX_VDISABLE && out == tty->params.control_chars[TCC_VEOF]) ||
                (tty->params.control_chars[TCC_VEOL] != _POSIX_VDISABLE && out == tty->params.control_chars[TCC_VEOL])) {
                    return i - (char*)s;
                }
        }
        if (out == 256) { // signal occured - interrupted sleep
            tty->read_remaining = 0;
            if (i - (char*)s == 0)
                return -EINTR;
            return i - (char*)s;
        }
        *i = out;

        if (tty->params.lmodes & TTY_L_ICANON && out == '\n') {
            tty->read_remaining = 0;
            return i - (char*)s + 1;
        }
    }
    tty->read_remaining = 0;
    return n;
}


ssize_t tty_write(file_descriptor_t * file, const void * s, size_t n) { // outputs data - writes data into write queue
    // likewise assuming now file is a valid pointer
    dev_t dev = file->inode->device;
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE)) dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0);
    if (n == 0) return 0;

    kassert(current_process);

    if (current_process->ring != 0 && current_process->pgrp != terminals[MINOR(dev)]->foreground_pgrp) { // allow the kernel to write regardless
        signal_process_group(current_process->pgrp, &(siginfo_t) {.si_signo = SIGTTOU });
        return -EINTR;
    }

    size_t ret = tty_translate_line_outgoing(s, n, terminals[MINOR(dev)]);
    if (ret == 0 && n != 0) return -EINTR;
    return ret;
}
long tty_write_to_tty(const char * s, size_t n, dev_t dev) { // writes data into read queue of a tty, aka recv input
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE)) dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0);
        // since the underlying tty is the same for S0 and 0, having both would input stuff 2 times
    if (n == 0) return 0;

    if (!is_valid_tty(dev)) return -EINVAL;

    if (!s) return -EINVAL;


    size_t ret = tty_translate_line_incoming(s, n, terminals[MINOR(dev)]);
    if (ret == 0 && n != 0) return -EINTR;
    return ret;
}