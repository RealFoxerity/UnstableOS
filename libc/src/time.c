#include <time.h>
#include <sys/times.h>
#include <unistd.h>
#include <UnstableOS/syscalls.h>
#include <errno.h>

int nanosleep(const struct timespec * rqtp, struct timespec * rmtp) {
    return clock_nanosleep(CLOCK_MONOTONIC, 0, rqtp, rmtp);
}

time_t time(time_t * tloc) {
    if (tloc == NULL) tloc = &(time_t){0};
    int ret = syscall(SYSCALL_TIME, tloc);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return *tloc;
}

clock_t clock() {
    struct tms info = {0};
    clock_t uptime = times(&info);


    if (uptime >= 0xFFFFFFFFFFFFFF00ULL) {
        ___set_errno((int)(unsigned long long)uptime);
        return -1;
    } // assuming at most 255 errnos

    return info.tms_utime + info.tms_stime;
}


struct tm * gmtime(const time_t *timer) {
    static struct tm tm = {0};
    return gmtime_r(timer, &tm);
}

// credit to @nikifemme on discord for this wonderful implementation
struct tm * gmtime_r(const time_t * __restrict timer, struct tm * __restrict result) {
    time_t d = *timer / 86400;
    int sod = *timer % 86400;

    // fix pre-1970 wrap
    if (sod < 0) {
        sod += 86400;
        d -= 1;
    }

    // hh:mm:ss
    result->tm_sec = sod % 60;
    result->tm_min = (sod / 60) % 60;
    result->tm_hour = sod / 3600;

    // day of week
    int wday = (d + 4) % 7;
    result->tm_wday = wday < 0 ? wday + 7 : wday;

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
    result->tm_mday = doy - (153 * mp + 2) / 5 + 1;

    // map back to posix
    int is_jan_feb = (mp >= 10) ? 1 : 0;
    result->tm_mon = is_jan_feb ? mp - 10 : mp + 2;

    y += is_jan_feb;
    result->tm_year = y - 1900;

    // yday leap adjust
    int leap_adj = ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 1 : 0;
    result->tm_yday = is_jan_feb ? doy - 306 : doy + 59 + leap_adj;

    result->tm_isdst = -1; // not available

    return result;
}

// TODO: we don't yet support timezones
struct tm * localtime(const time_t * timer) {
    return gmtime(timer);
}
struct tm * localtime_r(const time_t * __restrict timer, struct tm * __restrict result) {
    return gmtime_r(timer, result);
}


static time_t year_to_time(long long year, char * is_leap) {
    char __is_leap;
    if (!is_leap)
        is_leap = &__is_leap;
    year -= 70; // posix tm is 1900 based

    long long leaps = (year + (year < 0 ? -2 : 2)) / 4;
    leaps -= year < 0 ? (year - 30) / 100 : (year + 70)  / 100;
    leaps += year < 0 ? (year - 30) / 400 : (year + 370) / 400;
    //leaps -= year < 0 ? (year - 2030) / 4000 : (year + 1970) / 4000;
    *is_leap = ((year - 2) % 4 == 0) &&
        ((year - 30) % 100 != 0 || (year - 30) % 400 == 0)
        /*&& (year - 2030) % 4000 != 0 */;
    if (*is_leap && year >= 0) leaps--;
    return (year * 365 + leaps) * 24 * 60 * 60;
}

static time_t date_to_time(int mon, int day, char is_leap) {
    if (mon > 11)
        return 0;
    static int days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int i = 0; i < mon; i++)
        day += days_in_month[i];
    if (is_leap && mon >= 2) day++;
    return (day - 1) * 24 * 60 * 60;
}

time_t mktime(struct tm * timeptr) {
    char is_leap;
    time_t ret = year_to_time(timeptr->tm_year, &is_leap);
    if (timeptr->tm_yday)
        ret += timeptr->tm_yday * 24 * 60 * 60;
    else
        ret += date_to_time(timeptr->tm_mon, timeptr->tm_mday, is_leap);
    ret += timeptr->tm_hour * 60 * 60;
    ret += timeptr->tm_min * 60;
    ret += timeptr->tm_sec;
    return ret;
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

int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *rqtp, struct timespec *rmtp) {
    int ret = syscall(SYSCALL_CLOCK_NANOSLEEP, clock_id, flags, rqtp, rmtp);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int clock_getres(clockid_t clock_id, struct timespec *res) {
    int ret = syscall(SYSCALL_CLOCK_GETRES, clock_id, res);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    int ret = syscall(SYSCALL_CLOCK_GETTIME, clock_id, tp);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int clock_settime(clockid_t clock_id, const struct timespec *tp) {
    int ret = syscall(SYSCALL_CLOCK_SETTIME, clock_id, tp);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}