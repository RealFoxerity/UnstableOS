#ifndef _TIME_H
#define _TIME_H

#include "sys/types.h"

#define CLOCKS_PER_SEC 1024 // kernel.h RTC_TIMER_RESOLUTION_HZ

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

int nanosleep(const struct timespec * rqtp, struct timespec * rmtp);
time_t time(time_t * tloc);
clock_t clock();

struct tm * gmtime(const time_t *timer);
struct tm * gmtime_r(const time_t * __restrict timer, struct tm * __restrict result);
time_t mktime(struct tm * timeptr);

char * asctime(const struct tm * timeptr);
char * asctime_r(const struct tm * __restrict timeptr, char * __restrict buf);

struct tm * localtime(const time_t * timer);
struct tm * localtime_r(const time_t * __restrict timer, struct tm * __restrict result);

char * ctime(const time_t * clock);
char * ctime_r(const time_t * clock, char * buf);

#endif