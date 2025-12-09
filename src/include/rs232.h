#ifndef RS232_H
#define RS232_H

#define COM_MAX_BAUDRATE 115200

enum com_errors {
    COM_ERR_INVALID_PORT = -1,
    COM_ERR_INVALID_BAUDRATE = -2,
    COM_ERR_BAUDRATE_TOO_LOW = -3,
    COM_ERR_INVALID_DATA_BITS = -4,
    COM_ERR_INVALID_STOP_BITS = -5,
    COM_ERR_INVALID_PARITY = -6,
    COM_ERR_INVALID_CACHING = -7
};

enum com_parity { // don't move around
    COM_PARITY_NONE,
    COM_PARITY_ODD,
    COM_PARITY_EVEN,
    COM_PARITY_MARK,
    COM_PARITY_SPACE,
};

enum com_fifo { // don't move around, see COM_FCR_IRQ_LEVEL
    COM_BUFFER_1,
    COM_BUFFER_4,
    COM_BUFFER_8,
    COM_BUFFER_14
};

char com_init(unsigned char com, unsigned int baudrate, unsigned char data_bits, unsigned char stop_bits, unsigned char parity, unsigned char buffered_bytes);
long com_write(unsigned char com, const char * data, unsigned long len);
long com_read(unsigned char com, char * data_out, unsigned long len);

#define COM_DELTA_RX 0
#define COM_DELTA_TX 0
#define COM_DELTA_IRQ_EN 1
#define COM_DELTA_DLAB_LSB_BAUD 0
#define COM_DELTA_DLAB_MSB_BAUD 1
#define COM_DELTA_IIR 2 // interrupt identification register, info about the current state
#define COM_DELTA_FIFO_CONTROL 2
#define COM_DELTA_LINE_CONTROL 3 // MSB is dlab
#define COM_DELTA_MODEM_CONTROL 4
#define COM_DELTA_LINE_STATUS 5
#define COM_DELTA_MODEM_STATUS 6
#define COM_DELTA_SCRATCH_REGISTER 7

#define COM_MSB_DLAB_BIT_MASK (1<<7) // divisor latch access bit
#define COM_MSB_BREAK_BIT_MASK (1<<6) // while this bit is set, the trasmit line is held low, not that useful

#define COM_IRQ_EN_RECV_DATA_AVAIL 1
#define COM_IRQ_EN_TX_HOLD_REG_EMPTY (1<<1) // interrupt when we can send data
#define COM_IRQ_EN_RECV_LINE_STATUS (1<<2)
#define COM_IRQ_EN_MODEM_STATUS (1<<3)

#define COM_IIR_NO_PENDING 1
#define COM_IIR_INTERRUPT_STATE_MASK (3<<1) // 0, lowest priority, Modem Status, 1 = Tx holding register empty, 2 = recv data avail, 3 = reciever status
#define COM_IIR_UART_16650_TIMEOUT_IRQ_PENDING (1<<3)
#define COM_IIR_FIFO_BUFFER_STATE (3<<6) // 0 = no fifo, 1 = fifo enabled but unusable, 2 = fifo enabled

#define COM_FCR_ENABLE_FIFO 1
#define COM_FCR_CLEAR_RX_FIFO (1<<1) // will be set back to 0 once finished
#define COM_FCR_CLEAR_TX_FIFO (1<<2) // will be set back to 0 once finished
#define COM_FCR_DMA_MODE_SEL (1<<3) // if set enables DMA, so far no clue how to actually implement
#define COM_FCR_IRQ_LEVEL (3<<6) // how much data has to be recv before interrupt, 1, 4, 8, 14

#define COM_MCR_DTR 1 // data terminal ready
#define COM_MCR_RTS (1<<1) // request to send
#define COM_MCR_OUT1 (1<<2) // hardware pin OUT 1, unused in PC
#define COM_MCR_OUT2 (1<<3) // hardware pin OUT 2, can be used to enable IRQ in PC
#define COM_MCR_LOOP (1<<4) // local loopback for testing

#define COM_LSR_DATA_READY 1
#define COM_LSR_OVERRUN_ERROR (1<<1) // data has been lost
#define COM_LSR_PARITY_ERROR (1<<2)
#define COM_LSR_FRAMING_ERROR (1<<3) // stop bit was missing
#define COM_LSR_BREAK_INDICATOR (1<<4) // break in data input, see COM_MSB_BREAK_BIT_MASK
#define COM_LSR_TX_HOLDING_REGISTER_EMPTY (1<<5) // data can be sent
#define COM_LSR_TX_EMPTY (1<<6) // transmitter is not doing anything
#define COM_LSR_IMPENDING_ERROR (1<<7) // error with word in input buffer

#define COM_MSR_DELTA_CTS 1 // CTS input has changed since last time read
#define COM_MSR_DELTA_DSR (1<<1) // DSR input has changed
#define COM_MSR_TRAIL_EDGE_RING_IND (1<<2) // ring indicator input changed from low to high
#define COM_MSR_DELTA_CARRIER_DETECT (1<<3) // DCD input has changed
#define COM_MSR_CTS (1<<4) // inverted CTS
#define COM_MSR_DSR (1<<5) // inverted DSR
#define COM_MSR_RI (1<<6) // inverted RI
#define COM_MSR_DCD (1<<7) // inverted DCD
#endif