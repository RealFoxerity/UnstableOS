#include "include/kernel_interrupts.h"
#include "include/lowlevel.h"
#include "include/kernel.h"
#include "../libc/src/include/stdio.h"
#include "../libc/src/include/string.h"
#include "include/timer.h"

#include <stdint.h>
#include <stddef.h>

#define kprintf(fmt, ...) kprintf("Timer: "fmt, ##__VA_ARGS__)

char timer_init(char channel_id, uint16_t frequency_reload_val, uint8_t timer_mode) {
    if (channel_id > 2) {
        kprintf("Tried to set up invalid channel (%d)\n", channel_id);
        return -1;
    }
    if (timer_mode > TIMER_HW_STROBE) {
        kprintf("Tried to set an invalid timer mode (%d)\n", timer_mode);
        return -1;
    }

    if (channel_id == 1) kprintf("Warning: Timer 1 may not be supported by hardware\n");

    uint8_t com_reg = channel_id << 6; // channel
    com_reg |= 2 << 4; // lobyte, hibyte sent (in this order)
    com_reg |= timer_mode << 1;
    com_reg |= 0; // 16 bit binary number (1=4 digit BCD)

    disable_interrupts();

    uint16_t reload_value = frequency_reload_val;
    if (timer_mode >= TIMER_RATE) reload_value = (unsigned long)PIT_BASE_FREQUENCY/reload_value;

    kprintf("Setting channel %d to %u\n", channel_id, reload_value);

    outb(TIMER_MODE_PORT, com_reg);
    outb(TIMER_CHANNEL0_PORT+channel_id, reload_value&0xFF);
    outb(TIMER_CHANNEL0_PORT+channel_id, (reload_value>>8)&0xFF);

    enable_interrupts();
    return 0;
}