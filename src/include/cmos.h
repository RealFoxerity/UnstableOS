#ifndef CMOS_H
#define CMOS_H

#define CMOS_IO_PORT_REG_SELECT_DISABLE_NMI_MASK 0x80
#define CMOS_IO_PORT_REG_SELECT 0x70
#define CMOS_IO_PORT_RW 0x71

// return values are BCD or normal hex based on bit 2 of register B, values given below in hex mode
// 12/24hr mode based on bit 1 of register B
// alarm triggers if all three alarm registers match
// if we don't care for alarms, put values between 0xC0 - 0xFF
enum cmos_registers {
    CMOS_RTC_SECONDS,
    CMOS_RTC_SECOND_ALARM,
    CMOS_RTC_MINUTES,
    CMOS_RTC_MINUTE_ALARM,
    CMOS_RTC_HOURS, // 0x01 - 0x0C -> 1-12 AM, 0x81 - 0x8C - 1-12 PM in 12 hr mode, 0x00 - 0x17 in 24 hour mode
    CMOS_RTC_HOUR_ALARM,
    CMOS_RTC_DAY_OF_WEEK, // 1 = sunday
    CMOS_RTC_DAY_OF_MONTH,
    CMOS_RTC_MONTH,
    CMOS_RTC_YEAR,  // 0x00 - 0x63 -> 00 - 99
    CMOS_RTC_REG_A, // we don't really care about register A, because we usually don't want to change the timings + some chips don't even support it
    CMOS_RTC_REG_B,
    CMOS_RTC_REG_C,
    CMOS_RTC_REG_D,

    CMOS_FDD_TYPE = 0x10,

};

#define RTC_REG_A_UPDATE_IN_PROGRESS 0x80

enum rtc_reg_b {
    RTC_REG_B_DAYLIGHT_SAVINGS = 1,
    RTC_REG_B_24_HR_MODE = 2,
    RTC_REG_B_BINARY_MODE = 4,
    RTC_REG_B_SQUARE_WAVE = 8,
    RTC_REG_B_EN_UPDATE_ENDED_INT = 0x10,
    RTC_REG_B_EN_ALARM_INT = 0x20,
    RTC_REG_B_EN_PERIODIC_INT = 0x40,
    RTC_REG_B_CLOCK_SET_BY_FREEZE = 0x80 // freezes update to allow setting of a value
};
enum rtc_reg_c {
    RTC_REG_C_UPDATE_ENDED_INT_FLAG = 0x10,
    RTC_REG_C_ALARM_INT_FLAG = 0x20,
    RTC_REG_C_PERIODIC_INT_FLAG = 0x40,
    RTC_REG_C_INT_REQ_FLAG = 0x80,
};
#define RTC_REG_D_BATTERY_OK 0x80

enum fdd_types {
    FDD_NO_DRIVE,
    FDD_525_360KB,
    FDD_525_1200KB,
    FDD_35_720KB,
    FDD_35_1440KB,
    FDD_35_2880KB,
};

#include <stdint.h>

unsigned char cmos_get_register(unsigned char cmos_reg); // disable interrupts beforehand
void cmos_set_register(unsigned char cmos_reg, uint8_t data); // disable interrupts beforehand

void get_fdd_type(enum fdd_types * fdd1, enum fdd_types * fdd2);
#endif