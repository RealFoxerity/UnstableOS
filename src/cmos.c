#include "include/cmos.h"
#include "include/kernel.h"
#include "include/kernel_interrupts.h"
#include "include/lowlevel.h"
#include "include/timer.h"
#include <stdint.h>
// TODO: probably not thread safe, redo?
// TODO: test if 12 hour format actually works, i have a feeling it doesn't :P
#define kprintf(fmt, ...) kprintf("CMOS: "fmt, ##__VA_ARGS__)

static char nmi_enabled = 1;

static char rtc_uses_24h = 1, rtc_uses_binary = 1;

static inline void cmos_select_register(unsigned char cmos_reg) {
    cmos_reg |= !nmi_enabled ? CMOS_IO_PORT_REG_SELECT_DISABLE_NMI_MASK : 0;
    outb(CMOS_IO_PORT_REG_SELECT, cmos_reg);
}

unsigned char cmos_get_register(unsigned char cmos_reg) {
    cmos_select_register(cmos_reg);
    return inb(CMOS_IO_PORT_RW);
}
void cmos_set_register(unsigned char cmos_reg, uint8_t data) {
    cmos_select_register(cmos_reg);
    outb(CMOS_IO_PORT_RW, data);
}

void enable_nmi() {
    nmi_enabled = 1;
    cmos_get_register(0); // arbitrary register, doesn't matter, we need to set the nmi bit
}
void disable_nmi() {
    nmi_enabled = 0;
    cmos_get_register(0);
}

void get_fdd_type(enum fdd_types * fdd1, enum fdd_types * fdd2) {
    kassert(fdd1);
    kassert(fdd2);
    unsigned char fdd_types = cmos_get_register(CMOS_FDD_TYPE);
    *fdd2 = fdd_types & 0xF;
    *fdd1 = fdd_types >> 4;
}

enum rtc_interrupt_bitmasks rtc_get_last_interrupt_type() {
    enum rtc_interrupt_bitmasks recv_interrupt = cmos_get_register(CMOS_RTC_REG_C);
    if (!(recv_interrupt & 0x80)) return 0; // shouldn't happen but in case data sticks around
    return recv_interrupt & 0x70;
}

void rtc_set_divider(unsigned char rate_selection_divider, unsigned char time_base_divider) {
    if (rate_selection_divider >= 1 << 4 || (rate_selection_divider < 3 && rate_selection_divider != 0)) {
        kprintf("Invalid RTC rate selection divider specified, ignoring request! (%d)\n", rate_selection_divider);
        return;
    }
    if (time_base_divider >= 1 << 3) {
        kprintf("Invalid RTC time base divider specified, ignoring request! (%d)\n", time_base_divider);
        return;
    }
    disable_interrupts();
    //uint16_t frequency = (1<<15)>>(rate_selection_divider - 1);
    uint8_t new_reg_a = (time_base_divider & 0b111) << 4;
    new_reg_a |= rate_selection_divider & 0xF;

    cmos_set_register(CMOS_RTC_REG_A, new_reg_a);
    enable_interrupts();
}

static inline unsigned char bcd_to_int(unsigned char value) {
    unsigned char out = (value >> 4) * 10;
    out += value & 0xF;
    return out;
}

static inline unsigned char int_to_bcd(unsigned char value) {
    unsigned char out = (value / 10) << 4;
    out += value % 10;
    return out;
}

void rtc_set_alarm(unsigned char seconds, unsigned char minutes, unsigned char hours) {
    if (seconds >= 60 || minutes >= 60 ||hours == 0 || hours >= 24) {
        kprintf("Invalid RTC alarm options specified, ignoring request! (%dh %dm %ds)\n", hours, minutes, seconds);
        return;
    }

    if (!rtc_uses_binary) {
        seconds = int_to_bcd(seconds);
        minutes = int_to_bcd(minutes);
        if (rtc_uses_24h) hours = int_to_bcd(hours);
        else {
            if (hours >= 12) {
                if (hours != 12) hours = hours - 12; // i hate 12 am/pm
                hours = int_to_bcd(hours);
                hours |= 0x80;
            } else {
                if (hours == 0) hours = 12;
                hours = int_to_bcd(hours);
            }
        }
    } else if (!rtc_uses_24h) {
        hours |= 0x80;
    }

    disable_interrupts();
    cmos_set_register(CMOS_RTC_SECOND_ALARM, seconds);
    cmos_set_register(CMOS_RTC_MINUTE_ALARM, seconds);
    cmos_set_register(CMOS_RTC_HOUR_ALARM, seconds);

    enable_interrupts();
}

void rtc_enable_interrupt(enum rtc_interrupt_bitmasks interrupts) {
    disable_interrupts();
    uint8_t reg_b = cmos_get_register(CMOS_RTC_REG_B);
    reg_b |= interrupts;
    cmos_set_register(CMOS_RTC_REG_B, reg_b);
    enable_interrupts();
}
void rtc_disable_interrupt(enum rtc_interrupt_bitmasks interrupts) {
    disable_interrupts();
    uint8_t reg_b = cmos_get_register(CMOS_RTC_REG_B);
    reg_b &= ~interrupts;
    cmos_set_register(CMOS_RTC_REG_B, reg_b);
    enable_interrupts();
}

void rtc_set_daylight_savings(char enabled) {
    disable_interrupts();
    uint8_t reg_b = cmos_get_register(CMOS_RTC_REG_B);
    reg_b &= ~RTC_REG_B_DAYLIGHT_SAVINGS;
    reg_b |= enabled?RTC_REG_B_DAYLIGHT_SAVINGS:0;
    cmos_set_register(CMOS_RTC_REG_B, reg_b);
    enable_interrupts();
}

static inline void rtc_set_options(char binary, char longhours, char square_wave) {
    disable_interrupts();
    uint8_t reg_b = 
        binary ? RTC_REG_B_BINARY_MODE : 0 |
        longhours ? RTC_REG_B_24_HR_MODE : 0 |
        square_wave ? RTC_REG_B_SQUARE_WAVE : 0;
    cmos_set_register(CMOS_RTC_REG_B, reg_b);

    uint8_t actual_settings = cmos_get_register(CMOS_RTC_REG_B);
    if (actual_settings & RTC_REG_B_24_HR_MODE) rtc_uses_24h = 1;
    else rtc_uses_24h = 0;
    if (actual_settings & RTC_REG_B_BINARY_MODE) rtc_uses_binary = 1;
    else rtc_uses_binary = 0;

    if (rtc_uses_binary != binary) kprintf("Warning: RTC chip refused %s data format option\n", binary?"binary":"BCD");
    if (rtc_uses_24h != longhours) kprintf("Warning: RTC chip refused %s hour format option\n", longhours?"24":"12");
    enable_interrupts();
}


static int days_in_month(int month, int year) { // 1 being January, year being an actual year (e.g. 1972)
    if (month <= 7) {
        if (month == 2) {
            if (year % 4 == 0 && year % 100 != 0) return 29;
            else return 28;
        } else {
            if (month % 2 == 1) return 31;
            else return 30;
        }
    } else {
        if (month % 2 == 0) return 31;
        else return 30;
    }
}

static inline int days_to_month(int month, int year) {
    int days = 0;
    for (int i = 1; i < month; i++) {
        days += days_in_month(i, year);
    }
    return days;
}

size_t rtc_get_time() {
    disable_interrupts();
    while (cmos_get_register(CMOS_RTC_REG_A) & RTC_REG_A_UPDATE_IN_PROGRESS);

    unsigned char seconds = cmos_get_register(CMOS_RTC_SECONDS);
    unsigned char minutes = cmos_get_register(CMOS_RTC_MINUTES);
    unsigned char hours = cmos_get_register(CMOS_RTC_HOURS);
    unsigned char year = cmos_get_register(CMOS_RTC_YEAR);
    unsigned char weekday = cmos_get_register(CMOS_RTC_DAY_OF_WEEK);
    unsigned char monthday = cmos_get_register(CMOS_RTC_DAY_OF_MONTH);
    unsigned char month = cmos_get_register(CMOS_RTC_MONTH);
    enable_interrupts();

    if (!rtc_uses_binary) {
        seconds = bcd_to_int(seconds);
        minutes = bcd_to_int(minutes);
        hours = (hours & 0x80) | bcd_to_int(hours & 0x7F);
        year = bcd_to_int(year);
        weekday = bcd_to_int(weekday);
        monthday = bcd_to_int(monthday);
        month = bcd_to_int(month);
    }
    if (!rtc_uses_24h && hours & 0x80) hours = ((hours & 0x7F) + 12) % 24;

    int actual_year; // year from 1970 to calculate time
    if (year >= 70) actual_year = year - 70;
    else actual_year = 30 + year;

    if (actual_year < 30 + 26) kprintf("Warning: Invalid RTC year (%d)!\n", 1970+actual_year); // 2026

    size_t timestamp = 
        actual_year * 365.25 * 24 * 60 * 60 +
        days_to_month(month, 1970+actual_year) * 24 * 60 * 60 + 
        (monthday - 1) * 24 * 60 * 60 +
        hours * 60 * 60 +
        minutes * 60 +
        seconds;
    kprintf("RTC date %d %02d/%02d %02d:%02d:%02d - epoch %d\n", 1970+actual_year, monthday, month, hours, minutes, seconds, timestamp);
    return timestamp;
}

void rtc_init() {
    rtc_set_options(1, 1, 1);
    rtc_set_daylight_savings(DEFAULT_DAYLIGHT_SAVINGS_STATE);
    rtc_set_divider(RTC_DEFAULT_RATE_SELECTION_DIVIDER, RTC_DEFAULT_TIME_BASE_DIVIDER);
    rtc_enable_interrupt(RTC_INT_PERIODIC);
    rtc_get_time();
    rtc_get_last_interrupt_type(); // eoi for potential missed interrupts
}
