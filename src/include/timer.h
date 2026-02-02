#ifndef TIMER_H
#define TIMER_H

// header for the PIT, not APIC timer, not HPET

#define TIMER_CHANNEL0_PORT 0x40
//#define TIMER_CHANNEL1_PORT 0x41
//#define TIMER_CHANNEL2_PORT 0x42
#define TIMER_MODE_PORT 0x43

#include <stdint.h>

#define PIT_BASE_FREQUENCY 1193182

enum timer_mode {
    TIMER_COUNTER,
    TIMER_HW_ONESHOT, // counting doesn't start until gate goes high, so only channel 2
    TIMER_RATE, // frequency [Hz]  = PIT_BASE_FREQUENCY / value
    TIMER_SQUARE_RATE, // the same, but square wave instead of one time trigger
    TIMER_SW_STROBE, // rate, but wraps to 0xFFFF instead of the reload value
    TIMER_HW_STROBE, // SW_STROBE but same as in HW_ONESHOT
};

char timer_init(char channel_id, uint16_t frequency, uint8_t timer_mode); // 0 if successful


// cmos.c
void rtc_init();
enum rtc_interrupt_bitmasks {
    RTC_INT_PERIODIC = 0x40,
    RTC_INT_ALARM = 0x20,
    RTC_INT_UPDATE_ENDED = 0x10
};

#define DEFAULT_DAYLIGHT_SAVINGS_STATE 1
void rtc_set_daylight_savings(char enabled);
void rtc_set_alarm(unsigned char seconds, unsigned char minutes, unsigned char hours); // you have to then enable the interrupt manually

#define RTC_DEFAULT_TIME_BASE_DIVIDER 2
#define RTC_DEFAULT_RATE_SELECTION_DIVIDER 6
void rtc_set_divider(unsigned char rate_selection_divider, unsigned char time_base_divider);

enum rtc_interrupt_bitmasks rtc_get_last_interrupt_type();
void rtc_enable_interrupt(enum rtc_interrupt_bitmasks interrupts);
void rtc_disable_interrupt(enum rtc_interrupt_bitmasks interrupts);
#endif