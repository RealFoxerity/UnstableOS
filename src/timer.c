#include "include/lowlevel.h"
#include "include/kernel.h"
#include "../libc/src/include/stdio.h"
#include "../libc/src/include/string.h"
#include "include/timer.h"

#include <stdint.h>
#include <stddef.h>

#define kprintf(fmt, ...) kprintf("Timer: "fmt, ##__VA_ARGS__)

char timer_init(char channel_id, uint16_t frequency, uint8_t timer_mode) {
    if (channel_id > 2) return -1;
    if (channel_id == 1) kprintf("Warning: Timer 1 may not be supported by hardware\n");


}