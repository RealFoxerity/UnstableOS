#include "kernel_tty_io.h"
#include <UnstableOS/devs.h>
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
#include <sys/ioctl.h>

spinlock_t tty_lock = {0};
tty_t * terminals[TTY_LIMIT_KERNEL] = {0};

long tty_open(inode_t * tty, unsigned short flags);
long tty_close(inode_t * tty);
static struct dev_operations tty_ops = {
    .pread = tty_pread,
    .pwrite = tty_pwrite,
    .ioctl = tty_ioctl,
    .open  = tty_open,
    .close = tty_close
};


static char is_valid_tty(dev_t dev) { // checks if the device is a valid raw tty - an actual tty object, so no meta ttys
    if (MAJOR(dev) != DEV_MAJ_TTY) return 0;
    if (MINOR(dev) >= TTY_LIMIT_KERNEL) return 0;
    if (terminals[MINOR(dev)] == NULL) return 0;
    if (terminals[MINOR(dev)]->write == NULL) return 0;

    return 1;
}

static tty_t * __tty_get_controlling_terminal(pid_t session) {
    for (int i = 0; i < TTY_LIMIT_KERNEL; i++) {
        if (terminals[i]->session == session) {
            return terminals[i];
        }
    }
    return NULL;
}

tty_t * tty_get_controlling_terminal(pid_t session) {
    spinlock_acquire_interruptible(&tty_lock);
    tty_t * tty = __tty_get_controlling_terminal(session);
    spinlock_release(&tty_lock);
    return tty;
}

char tty_assign_session(inode_t * tty, pid_t session) {
    kassert(tty);
    if (!S_ISCHR(tty->mode))
        return -1;
    dev_t dev = tty->device;
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE))
        dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0);
    if (!is_valid_tty(dev))
        return -1;
    tty_t * term = terminals[MINOR(dev)];
    spinlock_acquire_interruptible(&term->tty_lock);
    if (terminals[MINOR(dev)]->session != 0 || tty_get_controlling_terminal(session)) {
        spinlock_release(&term->tty_lock);
        return -1;
    }

    term->session = session;
    term->foreground_pgrp = session;

    spinlock_release(&term->tty_lock);
    return 0;
}

void tty_flush_input(tty_t * tty);

// TODO: add the missing unimplemented ones
// missing BRKINT, IGNBRK, IGNPAR, INPCK, unsure about whether to implement IXOFF?
#define TERMIOS_VALID_IFLAGS ( \
ICRNL | \
IGNCR | \
INLCR | \
ISTRIP | \
IXANY | \
IXON \
)

// missing nldly, crly, tabdly, bsdly, vtdly
#define TERMIOS_VALID_OFLAGS ( \
OPOST | \
ONLCR | \
OCRNL | \
ONOCR | \
ONLRET \
)

#define TERMIOS_VALID_LFLAGS ( \
ECHO | \
ECHOE | \
ECHOK | \
ECHONL | \
ICANON | \
ISIG | \
NOFLSH | \
TOSTOP | \
ECHOCTL \
)

// missing everything
#define TERMIOS_VALID_CFLAGS (0)

long tty_ioctl(file_descriptor_t * file, unsigned long request, void * arg) {
    kassert(file);
    kassert(file->inode);
    kassert(S_ISCHR(file->inode->mode));

    dev_t dev = file->inode->device;
    kassert(MAJOR(dev) == DEV_MAJ_TTY);

    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE)) dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0);

    if (!is_valid_tty(dev)) return -EINVAL; // no clue what to return here

    // ioctls which according to POSIX should send SIGTTOU to bg pgrp or otherwise special treatment
    switch (request) {
        case TIOCSPGRP:
            if (current_process->session != terminals[MINOR(dev)]->session)
                return -ENOTTY;
            if ((pid_t)arg <= 0) return -EINVAL;

            // this is vulnerable to races of setpgid/setsid from a different thread
            // of the target pgrp leader
            // meaning leaked foreground group; however that could happen anyway
            // so the added complexity is not worth it
            spinlock_acquire(&scheduler_lock);
            for (process_t * proc = process_list; proc != NULL; proc = proc->next) {
                if (proc->pgrp == (pid_t)arg) {
                    if (proc->session != terminals[MINOR(dev)]->session) {
                        spinlock_release(&scheduler_lock);
                        return -EPERM;
                    }
                    break;
                }
            }
            spinlock_release(&scheduler_lock);
        case TCSETS:
        case TCSETSF:
        case TCSETSW:
        case TCXONC:
        case TCFLSH:
            if (current_process->session != terminals[MINOR(dev)]->session ||
                current_process->pgrp    == terminals[MINOR(dev)]->foreground_pgrp)
                    break;
            if (current_process->sa_handlers[SIGTTOU].sa_handler == SIG_IGN) break;
            if (current_thread ->sa_mask & GET_SIG_MASK(SIGTTOU))            break;

            if (current_process->prgp_orphan) return -EIO;

            signal_process_group(current_process->pgrp, &(siginfo_t){
                .si_signo = SIGTTOU,
            });
            return -EINTR;
        case TIOCGPGRP:
        case TIOCGSID:
            if (current_process->session != terminals[MINOR(dev)]->session)
                return -ENOTTY;
        default: break;
    }

    switch (request) {
        case TCGETS:
            if (paging_check_address_range(arg, sizeof(struct termios), 1, 0) == 0)
                return -EFAULT;

            spinlock_acquire_interruptible(&tty_lock);
            memcpy(arg, &terminals[MINOR(dev)]->params, sizeof(struct termios));
            spinlock_release(&tty_lock);
            return 0;
        case TCSETS: // apply immediately
            if (paging_check_address_range(arg, sizeof(struct termios), 1, 0) == 0)
                return -EFAULT;

            spinlock_acquire_interruptible(&tty_lock);
            memcpy(&terminals[MINOR(dev)]->params, arg, sizeof(struct termios));
            terminals[MINOR(dev)]->params.c_iflag &= TERMIOS_VALID_IFLAGS;
            terminals[MINOR(dev)]->params.c_oflag &= TERMIOS_VALID_OFLAGS;
            terminals[MINOR(dev)]->params.c_lflag &= TERMIOS_VALID_LFLAGS;
            terminals[MINOR(dev)]->params.c_cflag &= TERMIOS_VALID_CFLAGS;
            spinlock_release(&tty_lock);
            return 0;
        case TCSETSW: // apply after writing all
            if (paging_check_address_range(arg, sizeof(struct termios), 1, 0) == 0)
                return -EFAULT;

            spinlock_acquire_interruptible(&tty_lock);

            terminals[MINOR(dev)]->write(terminals[MINOR(dev)]);

            memcpy(&terminals[MINOR(dev)]->params, arg, sizeof(struct termios));
            spinlock_release(&tty_lock);
            return 0;
        case TCSETSF: // apply after writing all and discarding unread input
            if (paging_check_address_range(arg, sizeof(struct termios), 1, 0) == 0)
                return -EFAULT;

            spinlock_acquire_interruptible(&tty_lock);

            terminals[MINOR(dev)]->write(terminals[MINOR(dev)]);

            __atomic_store(
                        &terminals[MINOR(dev)]->iqueue.head,
                        &terminals[MINOR(dev)]->iqueue.tail,
                        __ATOMIC_RELEASE
                    );
            tty_flush_input(terminals[MINOR(dev)]);

            memcpy(&terminals[MINOR(dev)]->params, arg, sizeof(struct termios));
            spinlock_release(&tty_lock);
            return 0;
        case TCXONC:
            switch ((long)arg) {
                case TCOOFF:
                    terminals[MINOR(dev)]->output_stopped = 1;
                    return 0;
                case TCOON:
                    terminals[MINOR(dev)]->output_stopped = 0;
                    thread_queue_unblock_all(&terminals[MINOR(dev)]->oqueue.ix_queue);
                    return 0;
                case TCIOFF:
                    tty_queue_putch(
                        &terminals[MINOR(dev)]->oqueue,
                        terminals[MINOR(dev)]->params.c_cc[VSTOP],
                        0
                    );
                    if (!terminals[MINOR(dev)]->output_stopped)
                        terminals[MINOR(dev)]->write(terminals[MINOR(dev)]);
                    return 0;
                case TCION:
                    tty_queue_putch(
                        &terminals[MINOR(dev)]->oqueue,
                        terminals[MINOR(dev)]->params.c_cc[VSTART],
                        0
                    );
                    if (!terminals[MINOR(dev)]->output_stopped)
                        terminals[MINOR(dev)]->write(terminals[MINOR(dev)]);
                    return 0;
                default:
                    return -EINVAL;
            }
        case TCFLSH:
            switch ((long)arg) {
                case TCIFLUSH:
                    __atomic_store(
                        &terminals[MINOR(dev)]->iqueue.head,
                        &terminals[MINOR(dev)]->iqueue.tail,
                        __ATOMIC_RELEASE
                    );
                    tty_flush_input(terminals[MINOR(dev)]);
                    return 0;
                case TCOFLUSH:
                    __atomic_store(
                        &terminals[MINOR(dev)]->oqueue.head,
                        &terminals[MINOR(dev)]->oqueue.tail,
                        __ATOMIC_RELEASE
                    );
#if TTY_QUEUE_MODE == 2
                    thread_queue_unblock_all(&terminals[MINOR(dev)]->oqueue.write_queue);
#endif
                    return 0;
                case TCIOFLUSH:
                    __atomic_store(
                        &terminals[MINOR(dev)]->iqueue.head,
                        &terminals[MINOR(dev)]->iqueue.tail,
                        __ATOMIC_RELEASE
                    );
                    __atomic_store(
                        &terminals[MINOR(dev)]->oqueue.head,
                        &terminals[MINOR(dev)]->oqueue.tail,
                        __ATOMIC_RELEASE
                    );
                    tty_flush_input(terminals[MINOR(dev)]);
#if TTY_QUEUE_MODE == 2
                    thread_queue_unblock_all(&terminals[MINOR(dev)]->oqueue.write_queue);
#endif
                    return 0;
                default:
                    return -EINVAL;
            }
        case TIOCGPGRP:
            return terminals[MINOR(dev)]->foreground_pgrp;
        case TIOCSPGRP:
            if ((pid_t)arg <= 0) return -EINVAL;

            int pgid_test = 0;
            spinlock_acquire(&scheduler_lock);
            for (process_t * process = process_list; process != NULL; process = process->next) {
                if (process->pid == (pid_t)arg) {
                    if (process->session != current_process->session)
                        pgid_test = 1;
                    break;
                }
            }
            spinlock_release(&scheduler_lock);

            if (pgid_test) return -EPERM;

            terminals[MINOR(dev)]->foreground_pgrp = (pid_t)arg;
            return 0;
        case TIOCGSID:
            return terminals[MINOR(dev)]->session;
        default:
            return -EINVAL;
    }
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
        console_write(tq->buffer + tq->head, MAX_CANON - tq->head);
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
        .com_port = com_port,
        .foreground_pgrp = foreground_pgrp,
        .height = height,
        .width = width,
        .session = controlling_session,
        .write = write,
        .params.c_iflag = imodes,
        .params.c_lflag = lmodes,
        .params.c_oflag = omodes,
    };
    memcpy(new_tty->params.c_cc, control_chars, sizeof(new_tty->params.c_cc));

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

static void tty_set_defaults(tty_t * tty) {
    tty->params.c_iflag = TTYDEF_IFLAG;
    tty->params.c_lflag = TTYDEF_LFLAG;
    tty->params.c_oflag = TTYDEF_OFLAG;
    tty->session = tty->foreground_pgrp = 0;
    memcpy(tty->params.c_cc, default_control_chars, sizeof(tty->params.c_cc));
}

long tty_open(inode_t * tty, unsigned short flags) {
    kassert(tty);
    if (!S_ISCHR(tty->mode))
        return -1;
    dev_t dev = tty->device;
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE))
        dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0);
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CURRENT)) {
        spinlock_acquire(&current_process->lock);
        dev = current_process->ctty;
        spinlock_release(&current_process->lock);
        if (dev == 0)
            return -ENXIO;
    }
    if (!is_valid_tty(dev))
        return -ENODEV;

    tty_t * term = terminals[MINOR(dev)];

    spinlock_acquire(&term->tty_lock);
    if (term->session == 0 && flags & O_TTY_INIT)
        tty_set_defaults(term);
    if (term->session == 0 && !(flags & (O_NOCTTY | O_SEARCH | O_PATH))) {
        spinlock_acquire(&current_process->lock);
        if (current_process->ctty == 0 && current_process->session == current_process->pid) {
            spinlock_acquire(&scheduler_lock);
            term->session = current_process->pid;
            term->foreground_pgrp = current_process->pgrp;

            // also sets current process
            for (process_t * proc = process_list; proc != NULL; proc = proc->next) {
                if (proc->session != current_process->pid)
                    continue;

                if (proc->ctty != 0) {
                    panic("Process in the same session with a different controlling terminal");
                }
                proc->ctty = dev;
            }
            spinlock_release(&scheduler_lock);
        }
        spinlock_release(&current_process->lock);
    }
    spinlock_release(&term->tty_lock);
    tty->dev_opened = 1;
    return 0;
}

long tty_close(inode_t * tty) {
    kassert(tty);
    if (!S_ISCHR(tty->mode))
        return -1;
    dev_t dev = tty->device;
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE))
        dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0);
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CURRENT)) {
        return 0; // pseudo device, will never actually "get open", so nothing to close
    }
    if (!is_valid_tty(dev))
        return -1;
    tty_t * term = terminals[MINOR(dev)];
    spinlock_acquire(&term->tty_lock);
    term->session = term->foreground_pgrp = 0;
    spinlock_release(&term->tty_lock);
    return 0;
}

void tty_alloc_kernel_console() { // for the kernel task, don't call for user processes
    if (kernel_task == NULL) panic("Tried to allocate console before initializing kernel task!");

    tty_t * kernel_console = tty_init_tty(
        TTYDEF_IFLAG,
        TTYDEF_LFLAG,
        TTYDEF_OFLAG,
        default_control_chars,
        display_height, display_width,
        tty_console_write, 0,
        0, 0);
    tty_register(kernel_console, DEV_TTY_0);
    tty_register(kernel_console, DEV_TTY_S0);

    tty_t * ttys1 = tty_init_tty(
        TTYDEF_IFLAG,
        TTYDEF_LFLAG,
        TTYDEF_OFLAG,
        default_control_chars,
        display_height, display_width,
        tty_com_write, 1,
        0, 0);
    tty_register(ttys1, DEV_TTY_S0 + 1);

    dev_register_ops(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE), &tty_ops);
    dev_register_ops(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CURRENT), &tty_ops);
}

int tty_queue_getch(struct tty_queue * tq, struct timespec timeout) { // if 256, got SIGALRM
    if (current_thread->sa_to_be_handled)
        return 256;
    
    kassert(tq->head < MAX_CANON && tq->tail < MAX_CANON);

    again:
    if (EMPTY(tq)) {
        if (timeout.tv_nsec == 0 && timeout.tv_sec == 0)
            thread_queue_add(&tq->read_queue, current_process, current_thread, SCHED_INTERR_SLEEP);
        else if (timeout.tv_nsec == -1)
            return 257;
        else
            if (thread_queue_add_with_timeout(&tq->read_queue, current_process, current_thread, timeout))
                return 257;
    }
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
#if TTY_QUEUE_MODE == 2
    thread_queue_unblock(&tq->write_queue);
#endif
    spinlock_release(&tq->queue_lock);
    return out;
}

int tty_queue_putch(struct tty_queue * tq, char c, char onlret) {
    if (current_thread->sa_to_be_handled)
        return 256;

    kassert(tq->head < MAX_CANON && tq->tail < MAX_CANON);
    spinlock_acquire_interruptible(&tq->queue_lock);
    while (FULL(tq)) { // buffer full
        //kprintf("tty buffer full, flushing\n");
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

    switch (c) {
        case '\v':
        case '\n':
            if (!onlret) break;
        case '\r':
            tq->tty_column = 0;
            break;
        default:
            tq->tty_column++;
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
            (tty->params.c_cc[VEOF] != _POSIX_VDISABLE && tty->iqueue.buffer[tty->iqueue.tail-1] == tty->params.c_cc[VEOF]) ||
            (tty->params.c_cc[VEOL] != _POSIX_VDISABLE && tty->iqueue.buffer[tty->iqueue.tail-1] == tty->params.c_cc[VEOL])
        )) {
            if (!is_vkill && tty->params.c_lflag & ECHO) {
                if (tty->params.c_lflag & ECHOE) {
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
                            if (tty->params.c_lflag & ECHOCTL) {
                                if (tty->iqueue.buffer[tty->iqueue.tail-1] < ' ' ||

                                    (tty->params.c_cc[VERASE] != _POSIX_VDISABLE &&
                                    tty->iqueue.buffer[tty->iqueue.tail-1] == tty->params.c_cc[VERASE])) // probably not gonna happen
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
                    if (tty_translate_line_outgoing((const char *)&tty->params.c_cc[VERASE], 1, tty) != 1) {
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
    if (tty->params.c_lflag & ECHO) {
        if (tty_translate_line_outgoing((const char *)&tty->params.c_cc[VKILL], 1, tty) != 1) return -1;
        if (tty->params.c_lflag & ECHOK)
            if (tty_translate_line_outgoing("\n", 1, tty) != 1) return -1;
    }
    return 0;
}

// TODO: check ordering of operations
static inline size_t tty_translate_line_incoming(const char * s, size_t n, tty_t * tty) {
    kassert(s);
    kassert(tty);

    for (size_t i = 0; i < n; i++) {
        char checked = s[i];

        if (tty->params.c_iflag & IXON) {
            if (tty->params.c_cc[VSTOP] != _POSIX_VDISABLE && checked == tty->params.c_cc[VSTOP]) {
                tty->output_stopped = 1;
                continue;
            }
            if (tty->params.c_cc[VSTART] != _POSIX_VDISABLE && checked == tty->params.c_cc[VSTART]) {
                tty->output_stopped = 0;
                thread_queue_unblock_all(&tty->oqueue.ix_queue);
                continue;
            }
            if (tty->params.c_iflag & IXANY) {
                tty->output_stopped = 0;
                thread_queue_unblock_all(&tty->oqueue.ix_queue);
            }

            if (tty->input_stopped) continue;
        }

        if (tty->params.c_lflag & ICANON) {
            if (tty->params.c_cc[VERASE] != _POSIX_VDISABLE && checked == tty->params.c_cc[VERASE]) {
                if (tty_remove_char(tty, 0) == -1) return i;
                continue;
            }
            if (tty->params.c_cc[VKILL] != _POSIX_VDISABLE && checked == tty->params.c_cc[VKILL]) {
                if (tty_remove_line(tty) == -1) return i;
                continue;
            }
        }

        if (tty->params.c_iflag & ISTRIP) checked &= 0x7F; // stripping top bit
        char final = checked;
        char skip_char = 0;
        switch (checked) {
            case '\r':
                if (tty->params.c_iflag & IGNCR) break;
                if (tty->params.c_iflag & ICRNL) {
                    final = '\n';
                    break;
                }
            case '\n':
                if (tty->params.c_iflag & INLCR) {
                    final = '\r';
                    break;
                }
            default:
                if (tty->params.c_lflag & ISIG) {
                    if (tty->params.c_cc[VINTR] != _POSIX_VDISABLE && checked == tty->params.c_cc[VINTR]) {
                        signal_process_group(tty->foreground_pgrp, &(siginfo_t) {.si_signo = SIGINT });
                        if (!(tty->params.c_lflag & NOFLSH)) tty_flush_input(tty);
                        return i;
                    }
                    if (tty->params.c_cc[VQUIT] != _POSIX_VDISABLE && checked == tty->params.c_cc[VQUIT]) {
                        signal_process_group(tty->foreground_pgrp, &(siginfo_t) {.si_signo = SIGQUIT });
                        if (!(tty->params.c_lflag & NOFLSH)) tty_flush_input(tty);

                        return i;
                    }
                    if (tty->params.c_cc[VSUSP] != _POSIX_VDISABLE && checked == tty->params.c_cc[VSUSP]) {
                        signal_process_group(tty->foreground_pgrp, &(siginfo_t) {.si_signo = SIGTSTP });
                        //if (!(tty->params.c_lflag & NOFLSH)) tty_flush_input(tty);
                        return i;
                    }
                }
        }
        if (skip_char) continue;

        if (tty_queue_putch(&tty->iqueue, final, 0) == 256) return i;
        if (tty->params.c_lflag & ECHO) {
            if (tty->params.c_lflag & ECHOCTL && final < ' ') {
                if ((tty->params.c_cc[VSTART] != _POSIX_VDISABLE &&
                    final == tty->params.c_cc[VSTART]) ||

                    (tty->params.c_cc[VSTOP] != _POSIX_VDISABLE &&
                    final == tty->params.c_cc[VSTOP]) ||

                    final == '\n' || final == '\t')
                {
                    if (tty_translate_line_outgoing(&final, 1, tty) != 1) return i;
                } else {
                    if (tty_translate_line_outgoing("^", 1, tty) != 1) return i;
                    if (tty_translate_line_outgoing(&(char){final + '@'}, 1, tty) != 1) return i;
                }
            } else if (tty->params.c_lflag & ECHOCTL &&
                    final == 0x7f)
            {
                if (tty_translate_line_outgoing("^?", 2, tty) != 2) return i;
            } else {
                if (tty_translate_line_outgoing(&final, 1, tty) != 1) return i;
            }
        } else if (tty->params.c_lflag & ECHONL && (final == '\n' || final == '\v'))
            if (tty_translate_line_outgoing(&final, 1, tty) != 1) return i;

        if (tty->params.c_lflag & ICANON &&
                (final == '\n' ||
                (tty->params.c_cc[VEOF] != _POSIX_VDISABLE && final == tty->params.c_cc[VEOF]) ||
                (tty->params.c_cc[VEOL] != _POSIX_VDISABLE && final == tty->params.c_cc[VEOL]))) {
            tty_flush_input(tty);
        } else if (!(tty->params.c_lflag & ICANON) /* &&
                    ((tty->params.c_cc[VMIN] != _POSIX_VDISABLE &&
                        REMAIN(&tty->iqueue) >= tty->params.c_cc[VMIN]) ||
                        tty->params.c_cc[VMIN] == _POSIX_VDISABLE)*/
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

    if (!(tty->params.c_oflag & OPOST)) { // to avoid useless switch
        for (size_t i = 0; i < n; i++) {
            if (tty_queue_putch(&tty->oqueue, s[i], 0) == 256) return i;

            if (!(tty->output_stopped && current_process->ring == 0))
                tty->write(tty);
        }
        return n;
    }

    for (size_t i = 0; i < n; i++) {
        // != 0 because we don't want to deadlock the kernel
        // kernel here because of kprintf and keyboard input
        // because of how we handle tty queues, we can probably afford a few chars when full
        // by just going through and not writing out the results instead of sleeping
        if (tty->output_stopped && current_process->ring != 0)
            thread_queue_add(&tty->oqueue.ix_queue, current_process, current_thread, SCHED_UNINTERR_SLEEP);

        switch (s[i]) {
            case '\r':
                if (tty->params.c_oflag & ONOCR && tty->oqueue.tty_column == 0)
                    break;
                if (tty->params.c_oflag & OCRNL) {
                    if (tty_queue_putch(&tty->oqueue, '\n', (tty->params.c_oflag & ONLRET) != 0) == 256) return i;
                    break;
                }
            case '\v': // vertical tab is usually the same as newline
            case '\n':
                if (tty->params.c_oflag & ONLCR) {
                    if (!(tty->params.c_oflag & OCRNL)) {
                        if (tty_queue_putch(&tty->oqueue, '\r', (tty->params.c_oflag & ONLRET) != 0) == 256) return i;
                    } else {
                        if (tty_queue_putch(&tty->oqueue, '\n', (tty->params.c_oflag & ONLRET) != 0) == 256) return i;
                    } // \n twice?
                    if (tty_queue_putch(&tty->oqueue, '\n', (tty->params.c_oflag & ONLRET) != 0) == 256) return i;
                    break;
                }
            default:
                if (tty_queue_putch(&tty->oqueue, s[i], 0) == 256) return i;
        }
        //if (tty->params.c_lflag & ICANON &&
        //        (s[i] == '\n' ||
        //        (tty->params.c_cc[VEOF] != _POSIX_VDISABLE && s[i] == tty->params.c_cc[VEOF]) ||
        //        (tty->params.c_cc[VEOL] != _POSIX_VDISABLE && s[i] == tty->params.c_cc[VEOL]))) {
        //    tty->write(tty);
        //}
        //if (FULL(&tty->oqueue) || !(tty->params.c_lflag & ICANON)) tty->write(tty);

        // the special case of IXON, output stopped, and we're the kernel
        if (!(tty->output_stopped && current_process->ring == 0))
            tty->write(tty);
    }

    return n;
}

ssize_t tty_pread(file_descriptor_t * file, void * s, size_t n, off_t offset) {
    if (offset < 0) return -EINVAL;
    if (offset != 0) return -ESPIPE;

    // assuming now file is a valid pointer
    dev_t dev = file->inode->device;
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE))
        dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0);
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CURRENT)) {
        spinlock_acquire(&current_process->lock);
        dev = current_process->ctty;
        spinlock_release(&current_process->lock);
        if (dev == 0)
            return -ENXIO;
    }
    if (!is_valid_tty(dev)) return -EINVAL;

    if (n == 0) return 0;

    kassert(current_process);

    tty_t * tty = terminals[MINOR(dev)];

    if (current_process->pgrp != tty->foreground_pgrp && current_process->session == tty->session) {
        signal_process_group(current_process->pgrp, &(siginfo_t) {.si_signo = SIGTTIN });
        return -EINTR;
    }

    while (!__atomic_compare_exchange_n(&tty->read_remaining, &(unsigned long){0}, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) asm volatile ("pause");

    int out = 0;
    struct timespec timeout = {0};
    if (!(tty->params.c_lflag & ICANON)) {
        if (tty->params.c_cc[VTIME] == 0 && tty->params.c_cc[VMIN] == 0) {
            timeout.tv_nsec = -1;
        } else {
            timeout.tv_sec  = tty->params.c_cc[VTIME] / 10;
            timeout.tv_nsec = (tty->params.c_cc[VTIME] % 10) * 100000000;
        }
        if (tty->params.c_cc[VTIME] == 0 && tty->params.c_cc[VMIN] != 0)
            n = tty->params.c_cc[VMIN];
    }

    for (char * i = s; i < (char*)s + n; i++) {
        out = tty_queue_getch(&tty->iqueue, timeout);
        if (tty->params.c_lflag & ICANON) {
            if ((tty->params.c_cc[VEOF] != _POSIX_VDISABLE && out == tty->params.c_cc[VEOF]) ||
                (tty->params.c_cc[VEOL] != _POSIX_VDISABLE && out == tty->params.c_cc[VEOL])) {
                    tty->read_remaining = 0;
                    return i - (char*)s;
                }
        } else {
            // POSIX general terminal interface 11.1.7 Non-canonical input processing case A
            // VTIME is an interbyte interval, have to read at least 1 byte before returning
            if (out == 257 && i == s && tty->params.c_cc[VTIME] != 0 && tty->params.c_cc[VMIN] != 0) {
                i--; // counteract the for loop iteration
                continue;
            }
            else if (out == 257      && tty->params.c_cc[VTIME] != 0 && tty->params.c_cc[VMIN] != 0) {
                tty->read_remaining = 0;
                return i - (char*)s;
            }

            // case B is handled by our "n" setup before this for

            // case C & D
            // C: VMIN is 0 and VTIME then serves as a complete read() timer for a single byte - handled by timeout
            // D: both VMIN and VTIME are 0, read() returns immediately if no bytes present - handled by timeout of -1
            if (out == 257 && tty->params.c_cc[VMIN] == 0) {
                tty->read_remaining = 0;
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

        if (tty->params.c_lflag & ICANON && out == '\n') {
            tty->read_remaining = 0;
            return i - (char*)s + 1;
        }
    }
    tty->read_remaining = 0;
    return n;
}


ssize_t tty_pwrite(file_descriptor_t * file, const void * s, size_t n, off_t offset) { // outputs data - writes data into write queue
    if (offset < 0) return -EINVAL;
    if (offset != 0) return -ESPIPE;

    // likewise assuming now file is a valid pointer
    dev_t dev = file->inode->device;
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE))
        dev = GET_DEV(DEV_MAJ_TTY, DEV_TTY_0);
    if (dev == GET_DEV(DEV_MAJ_TTY, DEV_TTY_CURRENT)) {
        spinlock_acquire(&current_process->lock);
        dev = current_process->ctty;
        spinlock_release(&current_process->lock);
        if (dev == 0)
            return -ENXIO;
    }
    if (!is_valid_tty(dev)) return -EINVAL;

    if (n == 0) return 0;

    kassert(current_process);

    if (terminals[MINOR(dev)]->params.c_lflag & TOSTOP &&
        current_process->ring != 0 &&
        current_process->pgrp != terminals[MINOR(dev)]->foreground_pgrp &&
        current_process->session == terminals[MINOR(dev)]->session) { // allow the kernel to write regardless
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