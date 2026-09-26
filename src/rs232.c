#include <stdint.h>
#include <stddef.h>
#include "rs232.h"
#include <UnstableOS/devs.h>
#include "kernel_tty_io.h"
#include "lowlevel.h"
#include "kernel.h"
#include "kernel_sched.h"
#include <errno.h>

// no locks needed as that's managed by the tty layer's ioctl locking

#define RS232_IO_TIMEOUT 1024

#define kprintf(fmt, ...) kprintf("RS-232 driver: "fmt, ##__VA_ARGS__)

#define COM_PORTS 8
const uint16_t com_addresses[COM_PORTS] =  {0x3F8, 0x2F8, 0x3E8, 0x2E8, 0x5F8, 0x4F8, 0x5E8, 0x4E8};

enum com_state {
    COM_UNINITIALIZED, // will just skip writes
    COM_INITIALIZED
};

// 0 meaning impossible on this hardware
const static unsigned int baudrate_divisors[CBAUD + 1] = {
    [B0]        = 0,
    [B50]       = 115200/50,
    [B75]       = 115200/75,
    [B110]      = 115200/110,               //faster by 0.029 baud, should be fine
    [B134]      = (int)(115200/134.5) + 1,  //I hate POSIX for this being 134.5, faster by 0.078 baud, should be fine
    [B150]      = 115200/150,
    [B200]      = 115200/200,
    [B300]      = 115200/300,
    [B600]      = 115200/600,
    [B1200]     = 115200/1200,
    [B1800]     = 115200/1800,
    [B2400]     = 115200/2400,
    [B4800]     = 115200/4800,
    [B9600]     = 115200/9600,
    [B19200]    = 115200/19200,
    [B38400]    = 115200/38400,
    [B57600]    = 115200/57600,
    [B115200]   = 1
};

static char com_states[COM_PORTS] = {COM_UNINITIALIZED};
static uint8_t line_status_regs[COM_PORTS] = {0};  // to find the differences on line status interrupts, primarily for break
char com_init(unsigned char com, unsigned int termios_baudrate, enum com_data_bits data_bits, enum com_stop_bits stop_bits, enum com_parity parity, enum com_fifo buffered_bytes) {
    if (com >= COM_PORTS) {
        kprintf("Invalid COM port to initialize specified (%d)!\n", com);
        return COM_ERR_INVALID_PORT;
    }

    // hangup
    if (termios_baudrate == 0) {
        // turn off interrupts, drop DCD/DTR
        outb(com_addresses[com] + COM_DELTA_MODEM_CONTROL, 0);
        outb(com_addresses[com] + COM_DELTA_IRQ_EN, 0);
        com_states[com] = COM_UNINITIALIZED;
        return 0;
    }
    if (termios_baudrate >= CBAUD + 1 || !baudrate_divisors[termios_baudrate]) {
        kprintf("Invalid baudrate specified for COM%d!\n", com);
        return COM_ERR_INVALID_BAUDRATE;
    }

    if (data_bits > COM_DATA_BITS_8) {
        kprintf("Invalid data bits count for COM%d!\n", com);
        return COM_ERR_INVALID_DATA_BITS;
    }

    if (stop_bits > COM_STOP_BITS_2) {
        kprintf("Invalid stop bits count for COM%d!\n", com);
        return COM_ERR_INVALID_STOP_BITS;
    }

    if (parity > COM_PARITY_SPACE) {
        kprintf("Invalid parity for COM%d!\n", parity);
        return COM_ERR_INVALID_PARITY;
    }

    if (buffered_bytes > COM_BUFFER_14) {
        kprintf("Invalid requested buffered byte count for COM%d!\n", com);
        return COM_ERR_INVALID_CACHING;
    }
    kprintf("Initializing port %d with baud rate %u\n", com, COM_MAX_BAUDRATE / baudrate_divisors[termios_baudrate]);

    // set the baud rate
    uint8_t brlow  = termios_baudrate &  0xFF;
    uint8_t brhigh = termios_baudrate >> 8;
    outb(com_addresses[com] + COM_DELTA_IRQ_EN, 0);
    outb(com_addresses[com] + COM_DELTA_LINE_CONTROL, COM_LCR_DLAB);
    io_wait();
    outb(com_addresses[com] + COM_DELTA_DLAB_LSB_BAUD, brlow);
    outb(com_addresses[com] + COM_DELTA_DLAB_MSB_BAUD, brhigh);
    io_wait();


    uint8_t line_control = (data_bits & 0x3) | ((stop_bits & 1) << 2) | ((parity & 0x7) << 3);
    outb(com_addresses[com] + COM_DELTA_LINE_CONTROL, line_control);

    uint8_t fifo_control = COM_FCR_ENABLE_FIFO | COM_FCR_CLEAR_RX_FIFO | COM_FCR_CLEAR_TX_FIFO | (buffered_bytes << 6);
    outb(com_addresses[com] + COM_DELTA_FIFO_CONTROL, fifo_control);

    outb(com_addresses[com] + COM_DELTA_MODEM_CONTROL, COM_MCR_LOOP); // enable loopback to test com port
    io_wait();

    outb(com_addresses[com], 0x06); //send test byte (0x06 is just a random byte)
    io_wait();

    for (int i = 0; i < RS232_IO_TIMEOUT; i++) io_wait();
    if (inb(com_addresses[com]) != 0x06) {
        kprintf("COM%d failed self test!\n", com);
        return 1;
    }

    uint8_t modem_control = COM_MCR_OUT1 | COM_MCR_OUT2 | COM_MCR_DTR;
    outb(com_addresses[com] + COM_DELTA_MODEM_CONTROL, modem_control);
    outb(com_addresses[com] + COM_DELTA_IRQ_EN, COM_IRQ_EN_RECV_DATA_AVAIL | COM_IRQ_EN_RECV_LINE_STATUS | COM_IRQ_EN_MODEM_STATUS);
    com_states[com] = COM_INITIALIZED;
    return 0;
}

int com_ctl(tty_t * tty, struct termios * tio) {
    kassert(tty && tio);
    unsigned int baud = tio->c_cflag & CBAUD;
    int cs            = tio->c_cflag & CSIZE;
    cs >>= 5;
    enum com_data_bits data_bits = cs;
    enum com_stop_bits stop_bits = tio->c_cflag & CSTOPB ? COM_STOP_BITS_2 : COM_STOP_BITS_1;
    enum com_parity par = tio->c_cflag & PARENB ?
        tio->c_cflag & PARODD ?
            COM_PARITY_ODD:
            COM_PARITY_EVEN
        : COM_PARITY_NONE;

    int ret = (int)com_init(tty->com_port, baud, data_bits, stop_bits, par, COM_BUFFER_1);
    if (ret < 0)
        return -EINVAL;

    if (ret > 0)
        return -EIO;

    return 0;
}

void com_brk(tty_t * tty, int set) {
    kassert(tty);
    uint8_t lcr = inb(com_addresses[tty->com_port] + COM_DELTA_LINE_CONTROL);
    lcr &= ~COM_LCR_BREAK;
    lcr |= set ? COM_LCR_BREAK : 0;
    outb(com_addresses[tty->com_port] + COM_DELTA_LINE_CONTROL, lcr);
}
void com_hup(tty_t * tty) {
    kassert(tty);
    // turn off interrupts, drop DCD/DTR
    outb(com_addresses[tty->com_port] + COM_DELTA_MODEM_CONTROL, 0);
    outb(com_addresses[tty->com_port] + COM_DELTA_IRQ_EN, 0);
    com_states[tty->com_port] = COM_UNINITIALIZED;
}
static inline char com_ready_to_write(unsigned char com) {
    return inb(com_addresses[com] + COM_DELTA_LINE_STATUS) & COM_LSR_TX_HOLDING_REGISTER_EMPTY;
}
static inline char com_ready_to_recv(unsigned char com) {
    return inb(com_addresses[com] + COM_DELTA_LINE_STATUS) & COM_LSR_DATA_READY;
}

#define EMPTY(tq) ((tq)->head == (tq)->tail)
#define DEC(tq) ((tq)->head = ((tq)->head+1)%MAX_CANON)

size_t tty_com_write(tty_t * tty) { // assumes tty queue to be locked
    if (com_states[(int)tty->com_port] == COM_UNINITIALIZED) return 0;

    size_t written = 0;
    while (!EMPTY(&tty->oqueue)) {
        com_write(tty->com_port, &tty->oqueue.buffer[tty->oqueue.head], 1);
        DEC(&tty->oqueue);
        written++;
    }

    return written;
}

long com_write(unsigned char com, const char * data, unsigned long len) {
    if (com >= COM_PORTS) {
        kprintf("Invalid COM port to write to specified (%d)!\n", com);
        return -1;
    }
    if (com_states[com] == COM_UNINITIALIZED) return 0;

    for (unsigned long i = 0; i < len; i++) {
        for (int _ = 0; _ < RS232_IO_TIMEOUT && !com_ready_to_write(com); _++) {}
        outb(com_addresses[com], data[i]); 
    }
    return len;
}


static spinlock_t com_driver_lock = {0};
static thread_t * com_driver_thread = NULL;
static volatile int com_pending = -1;
static __attribute__((noreturn)) void com_driver_loop() {
    while (1) {
        if (__builtin_expect(com_pending == -1, 0)) {
            spinlock_acquire(&com_driver_lock);
            if (com_pending != -1) {
                spinlock_release(&com_driver_lock);
                continue;
            }
            spinlock_release(&com_driver_lock);

            com_driver_thread->status = SCHED_UNINTERR_SLEEP;
            reschedule();
        } else {
            spinlock_acquire_interruptible(&com_driver_lock);

            uint8_t new_status = 0, diff = 0;
            char parmarked = 0;
            switch ((inb(com_addresses[com_pending] + COM_DELTA_IIR) & COM_IIR_INTERRUPT_STATE_MASK) >> 1) {
                case 0: // modem status
                    new_status = inb(com_addresses[com_pending] + COM_DELTA_MODEM_STATUS);

                    if (new_status & COM_MSR_DELTA_CARRIER_DETECT &&
                        !(new_status & COM_MSR_DCD))
                        tty_recv_hang_up(GET_DEV(DEV_MAJ_TTY, DEV_TTY_S0 + com_pending));
                    break;
                case 3: // line status
                    new_status = inb(com_addresses[com_pending] + COM_DELTA_LINE_STATUS);
                    diff = new_status ^ line_status_regs[com_pending];

                    if (new_status & (
                        COM_LSR_PARITY_ERROR |
                        COM_LSR_FRAMING_ERROR |
                        COM_LSR_IMPENDING_ERROR))
                            parmarked = 1;
                    // the double condition to not throw brkint on end of break
                    if (diff & COM_LSR_BREAK_INDICATOR &&
                        new_status & COM_LSR_BREAK_INDICATOR)
                        tty_recv_break(GET_DEV(DEV_MAJ_TTY, DEV_TTY_S0 + com_pending));
                    line_status_regs[com_pending] = new_status;
                    break;

                case 1: // transmit fifo empty
                case 2: // received data
                default:
                    break;
            }

            // in case stuff remains in the fifo on status interrupts
            while (com_ready_to_recv(com_pending)) {
                unsigned char data = inb(com_addresses[(int)com_pending]);
                tty_write_to_tty((char*)&data, 1, GET_DEV(DEV_MAJ_TTY, DEV_TTY_S0 + com_pending), parmarked);
                io_wait(); // just in case
            }

            asm volatile ("cli;");
            pic_send_eoi(PIC_INTERR_COM1);
            pic_send_eoi(PIC_INTERR_COM2);

            pic_unmask_irq(PIC_INTERR_COM1);
            pic_unmask_irq(PIC_INTERR_COM2);

            com_pending = -1;
            spinlock_release(&com_driver_lock);
        }
    }
}

void com_recv_byte(char com) { // called by interrupt
    if (com < 0 || com >= COM_PORTS) {
        kprintf("Invalid COM port specified from interrupt handler (%d)!\n", com);
        return;
    }
    if (inb(com_addresses[com_pending] + COM_DELTA_IIR) & COM_IIR_NO_PENDING)
        return;

    if (com_driver_thread == NULL) {
        spinlock_acquire(&scheduler_lock);
        com_driver_thread = kernel_create_thread(kernel_task, current_thread, (void (*)(void*))com_driver_loop, NULL, 0);
        spinlock_release(&scheduler_lock);
    }
    // so that we can do interruptible spinlocks
    switch (com) {
        case 0:
        case 1:
            pic_mask_irq(PIC_INTERR_COM1);
            pic_mask_irq(PIC_INTERR_COM2);
        default: break;
    }
    asm volatile ("sti");

    while (com_pending != -1) {com_driver_thread->status = SCHED_RUNNABLE; reschedule();}

    spinlock_acquire(&com_driver_lock);
    com_pending = com;
    com_driver_thread->status = SCHED_RUNNABLE;
    spinlock_release(&com_driver_lock);
}