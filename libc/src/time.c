#include "include/time.h"
#include "include/sys/times.h"
#include "include/unistd.h"
#include "../../src/include/kernel.h"
#include "include/errno.h"

int nanosleep(const struct timespec * rqtp, struct timespec * rmtp) {
    int ret = syscall(SYSCALL_NANOSLEEP, rqtp, rmtp);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

time_t time(time_t * tloc) {
    if (tloc == NULL) tloc = &(time_t){0};
    int ret = syscall(SYSCALL_TIME, tloc);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return *tloc;
}

clock_t clock() {
    struct tms info = {0};
    clock_t uptime = times(&info);


    if (uptime >= 0xFFFFFFFFFFFFFF00ULL) {
        errno = (int)(unsigned long long)uptime;
        return -1;
    } // assuming at most 255 errnos

    return (info.tms_utime + info.tms_stime) / CLOCKS_PER_SEC;
}


struct tm * gmtime(const time_t *timer) {
    static struct tm tm = {0};
    return gmtime_r(timer, &tm);
}

// credit to @nikifemme on discord for this wonderful implementation
struct tm * gmtime_r(const time_t * __restrict timer, struct tm * __restrict tm) {
    time_t d = *timer / 86400;
    int sod = *timer % 86400;

    // fix pre-1970 wrap
    if (sod < 0) {
        sod += 86400;
        d -= 1;
    }

    // hh:mm:ss
    tm->tm_sec = sod % 60;
    tm->tm_min = (sod / 60) % 60;
    tm->tm_hour = sod / 3600;

    // day of week
    int wday = (d + 4) % 7;
    tm->tm_wday = wday < 0 ? wday + 7 : wday;

    // shift epoch to 0000-03-01
    time_t z = d + 719468;

    // 400y era
    time_t era = (z >= 0 ? z : z - 146096) / 146097;
    time_t doe = z - era * 146097;
    time_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;

    time_t y = yoe + era * 400;
    time_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);

    // 153-day month bullshit
    time_t mp = (5 * doy + 2) / 153;
    tm->tm_mday = doy - (153 * mp + 2) / 5 + 1;

    // map back to posix
    int is_jan_feb = (mp >= 10) ? 1 : 0;
    tm->tm_mon = is_jan_feb ? mp - 10 : mp + 2;

    y += is_jan_feb;
    tm->tm_year = y - 1900;

    // yday leap adjust
    int leap_adj = ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 1 : 0;
    tm->tm_yday = is_jan_feb ? doy - 306 : doy + 59 + leap_adj;

    tm->tm_isdst = -1; // not available

    return tm;
}

// TODO: we don't yet support timezones
struct tm * localtime(const time_t * timer) {
    return gmtime(timer);
}
struct tm * localtime_r(const time_t * __restrict timer, struct tm * __restrict result) {
    return gmtime_r(timer, result);
}

time_t mktime(struct tm * timeptr) {
    struct tm dup = *timeptr;
    dup.tm_year += 70;
    if (dup.tm_year < 0) {
        dup.tm_sec *= -1;
        dup.tm_min *= -1;
        dup.tm_hour *= -1;
        dup.tm_yday *= -1;
    }

    return (
        (
            (
                dup.tm_year * 8766 +
                dup.tm_yday * 24 +
                dup.tm_hour
            ) * 60 +
            dup.tm_min
        ) * 60 +
        dup.tm_sec
    );
}

char * asctime(const struct tm * timeptr) {
    static char format_buf[32]; // posix says 26, but whatever
    return asctime_r(timeptr, format_buf);
}

// shamelessly stolen from POSIX :3
char * asctime_r(const struct tm * __restrict timeptr, char * __restrict buf) {
    static char wday_name[7][4] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static char mon_name[12][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    sprintf(buf, "%s %s%3d %.2d:%.2d:%.2d %d\n",
        wday_name[timeptr->tm_wday],
        mon_name[timeptr->tm_mon],
        timeptr->tm_mday, timeptr->tm_hour,
        timeptr->tm_min, timeptr->tm_sec,
        1900 + timeptr->tm_year);
    return buf;
}

char * ctime(const time_t * clock) {
    return asctime(localtime(clock));
}
char * ctime_r(const time_t * clock, char * buf) {
    struct tm temp;
    return asctime_r(localtime_r(clock, &temp), buf);
}