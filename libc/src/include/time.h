#ifndef _TIME_H
#define _TIME_H

#include "sys/types.h"

#define CLOCKS_PER_SEC 1024 // kernel.h RTC_TIMER_RESOLUTION_HZ

#define TIME_UTC 1
#define CLOCK_MONOTONIC 0
#define CLOCK_REALTIME 1
#define TIMER_ABSTIME 1

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

int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *rqtp, struct timespec *rmtp);
int clock_getres(clockid_t clock_id, struct timespec *res);
int clock_gettime(clockid_t clock_id, struct timespec *tp);
int clock_settime(clockid_t clock_id, const struct timespec *tp);

#endif