#include <stdint.h>
#include <stddef.h>
#include "include/rs232.h"
#include "include/devs.h"
#include "include/kernel_tty_io.h"
#include "include/lowlevel.h"
#include "include/kernel.h"

#define kprintf(fmt, ...) kprintf("RS-232 driver: "fmt, ##__VA_ARGS__)

#define COM_PORTS 8
const uint16_t com_addresses[COM_PORTS] =  {0x3F8, 0x2F8, 0x3E8, 0x2E8, 0x5F8, 0x4F8, 0x5E8, 0x4E8};

enum com_state {
    COM_UNINITIALIZED, // will just skip writes
    COM_INITIALIZED
};
char com_states[COM_PORTS] = {COM_UNINITIALIZED};
char com_init(unsigned char com, unsigned int baudrate, unsigned char data_bits, unsigned char stop_bits, unsigned char parity, unsigned char buffered_bytes) {
    if (com >= COM_PORTS) {
        kprintf("Invalid COM port to initialize specified (%d)!\n", com);
        return COM_ERR_INVALID_PORT;
    }
    if (baudrate == 0 || (COM_MAX_BAUDRATE % baudrate) != 0) {
        kprintf("Invalid baudrate for COM%d!\n", com);
        return COM_ERR_INVALID_BAUDRATE;
    }

    if (baudrate < 75) {
        kprintf("Baudrate way too low for COM%d!\n", com);
        return COM_ERR_BAUDRATE_TOO_LOW;
    }

    if (data_bits < 5 || data_bits > 8) {
        kprintf("Invalid data bits count for COM%d!\n", com);
        return COM_ERR_INVALID_DATA_BITS;
    }

    if (stop_bits != 1 && stop_bits != 2) {
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
    kprintf("Initializing port %d with baud rate %u\n", com, baudrate);

    // set the baud rate
    uint8_t brlow = baudrate&0xFF;
    uint8_t brhigh = baudrate >> 8;
    outb(com_addresses[com] + COM_DELTA_IRQ_EN, 0);
    outb(com_addresses[com] + COM_DELTA_LINE_CONTROL, COM_MSB_DLAB_BIT_MASK);
    io_wait();
    outb(com_addresses[com] + COM_DELTA_DLAB_LSB_BAUD, brlow);
    outb(com_addresses[com] + COM_DELTA_DLAB_MSB_BAUD, brhigh);
    io_wait();


    uint8_t line_control = data_bits | ((stop_bits == 2)<<2) | (parity << 3);
    outb(com_addresses[com] + COM_DELTA_LINE_CONTROL, line_control);

    uint8_t fifo_control = COM_FCR_ENABLE_FIFO | COM_FCR_CLEAR_RX_FIFO | COM_FCR_CLEAR_TX_FIFO | (buffered_bytes << 6);
    outb(com_addresses[com] + COM_DELTA_FIFO_CONTROL, fifo_control);

    outb(com_addresses[com] + COM_DELTA_MODEM_CONTROL, COM_MCR_LOOP); // enable loopback to test com port
    io_wait();

    outb(com_addresses[com], 0x06); //send test byte (0x06 is just a random byte)
    io_wait();
    if (inb(com_addresses[com]) != 0x06) {
        kprintf("COM%d failed self test!\n", com);
        return 1;
    }

    uint8_t modem_control = COM_MCR_OUT1 | COM_MCR_OUT2 | COM_MCR_RTS;
    outb(com_addresses[com] + COM_DELTA_MODEM_CONTROL, modem_control);
    outb(com_addresses[com] + COM_DELTA_IRQ_EN, COM_IRQ_EN_RECV_DATA_AVAIL);
    com_states[com] = COM_INITIALIZED;
    return 0;
}


static inline char com_ready_to_write(unsigned char com) {
    return inb(com_addresses[com] + COM_DELTA_LINE_STATUS) & COM_LSR_TX_HOLDING_REGISTER_EMPTY;
}

#define EMPTY(tq) ((tq)->head == (tq)->tail)
#define DEC(tq) ((tq)->head = ((tq)->head+1)%TTY_BUFFER_SIZE)

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
        while (!com_ready_to_write(com)); 
        outb(com_addresses[com], data[i]); 
    }
    return len;
}

void com_recv_byte(unsigned char com) { // called by interrupt
    if (com >= COM_PORTS) {
        kprintf("Invalid COM port specified from interrupt handler (%d)!\n", com);
        return;
    }
    char data = inb(com_addresses[com]);
    tty_write_to_tty(&data, 1, GET_DEV(DEV_MAJ_TTY, DEV_TTY_S0 + com));
}

long com_read(unsigned char com, char * data_out, unsigned long len) { // i guess technically not needed assuming we allocate a TTY for every single serial port
    if (com > COM_PORTS) {
        kprintf("Invalid COM port to read from specified (%d)!\n", com);
        return -1;
    }
    // TODO: to do
}