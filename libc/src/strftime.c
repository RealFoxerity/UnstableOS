#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

struct ftimecmd {
    unsigned char zero_pad : 1;
    unsigned char sign_pad : 1;
    unsigned char modifier; // E or O (letter o), ignored by POSIX/C format, TODO: change strftime when locales
    unsigned char conv_specifier;

    unsigned short shift; // how much was consumed in the parsing, 0 = invalid

    size_t padding;
};

static struct ftimecmd strftime_get_conv(const char * s) {
    const char * start = s;

    struct ftimecmd result = {0};
    if (*s != '%')
        return result;
    s++;
    if (*s == '0')
        result.zero_pad = 1;
    else if (*s == '+')
        result.sign_pad = 1;
    if (*s == '0' || *s == '+')
        s++;

    if (isdigit(*s)) {
        char * end;
        result.padding = strtoul(s, &end, 10);
        s = end;
    }
    if (*s == 'E' || *s == 'O') {
        result.modifier = *s;
        s++;
    }
    result.conv_specifier = *s;
    s++;
    result.shift = s - start;
    return result;
}

static const char * weekday_short[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
static const char * weekday_long [] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
static const char * month_short  [] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char * month_long   [] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                        "September", "October", "November", "December"};
static char is_leap_year(int year)
{
    year += 1900;
    return !(year % 4) && ((year % 100) || !(year % 400));
}

static int week_of_iso_year(const struct tm * timeptr) {
    // oh pray lord this is correct :pray:

    // get the day of Jan 1 and convert from 0 = sunday to 0 = monday
    int jan1 = (timeptr->tm_wday + 6 + 365 - timeptr->tm_yday - 1) % 7;
    int week = (timeptr->tm_yday + jan1) / 7;
    // Jan 4 is the "first day of the year",
    //  so for it to be inside the same week as Jan 1, Jan 1 can be at most a thursday
    if (jan1 <= 3)
        week++;
    if (week == 0) {
        week = 52;

        int dec31 = (jan1 - 1) % 7;
        if (dec31 == 3 || (dec31 == 4 && is_leap_year(timeptr->tm_year % 400 - 1)))
            week++;
    } else if (week == 53) {
        if (jan1 != 3 && (jan1 != 2 || !is_leap_year(timeptr->tm_year)))
            week = 1;
    }
    return week;
}

#define PREFERRED_FORMAT "%a %d %b %Y %H:%M:%S"
#define _12HR_FORMAT "%I:%M:%S %p"
#define PREFERRED_DATE "%m/%d/%Y"
#define PREFERRED_TIME _12HR_FORMAT

#define MAX_FMT_LEN 64
size_t strftime(char *restrict s, size_t maxsize, const char *restrict format, const struct tm *restrict timeptr) {
    if (!s || !format || !timeptr) {
        ___set_errno(EFAULT);
        return 0;
    }

    if (maxsize < 2) {
        ___set_errno(ERANGE);
        return 0;
    }

    size_t i = 0;
    char fmtbuf[MAX_FMT_LEN];
    while (i < maxsize) {
        if (!*format) {
            *s = '\0';
            i++;
            break;
        }
        struct ftimecmd cmd = strftime_get_conv(format);
        if (!cmd.shift) {
            *s++ = *format++;
            i++;
            continue;
        }
        if (cmd.padding > maxsize - i) {
            ___set_errno(ERANGE);
            break;
        }
        long temp = 0;
        switch (cmd.conv_specifier) {
            case 'a':
                strcpy(fmtbuf, weekday_short[(timeptr->tm_wday + 6) % 7]); // tm_wday 0 = sunday
                break;
            case 'A':
                strcpy(fmtbuf, weekday_long[(timeptr->tm_wday + 6) % 7]);
                break;
            case 'h':
            case 'b':
                strcpy(fmtbuf, month_short[timeptr->tm_mon % 12]);
                break;
            case 'B':
                strcpy(fmtbuf, month_long[timeptr->tm_mon % 12]);
                break;
            case 'c':
                temp = strftime(fmtbuf, MAX_FMT_LEN, PREFERRED_FORMAT, timeptr);
                if (temp == 0)
                    return 0;
                break;
            case 'C':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", abs((timeptr->tm_year + 1900) / 100));
                break;
            case 'd':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", timeptr->tm_mday);
                break;
            case 'D':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d/%02d/%02d", timeptr->tm_mon, timeptr->tm_mday, timeptr->tm_year % 100);
                break;
            case 'e':
                snprintf(fmtbuf, MAX_FMT_LEN, "%2d", timeptr->tm_mday);
                break;
            case 'F':
                snprintf(fmtbuf, MAX_FMT_LEN, "%04d-%02d-%02d", abs(timeptr->tm_year + 1900), timeptr->tm_mon, timeptr->tm_mday);
                break;
            case 'g':
            case 'G':
                temp = timeptr->tm_year + 1900;
                if (timeptr->tm_yday < 3 && week_of_iso_year(timeptr) != 1)
                    temp--;
                else if (timeptr->tm_yday > 360 && week_of_iso_year(timeptr) == 1)
                    temp++;

                if (temp < 0)
                    temp = -temp;

                if (cmd.conv_specifier == 'g') {
                    temp %= 100;
                    snprintf(fmtbuf, MAX_FMT_LEN, "%02d", temp);
                } else
                    snprintf(fmtbuf, MAX_FMT_LEN, "%04d", temp);
                break;
            case 'H':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", timeptr->tm_hour);
                break;
            case 'I':
                temp = timeptr->tm_hour % 12;
                if (temp == 0) temp = 12;
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", temp);
                break;
            case 'j':
                snprintf(fmtbuf, MAX_FMT_LEN, "%03d", timeptr->tm_yday + 1);
                break;
            case 'm':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", timeptr->tm_mon + 1);
                break;
            case 'M':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", timeptr->tm_min);
                break;
            case 'n':
                strcpy(fmtbuf, "\n");
                break;
            case 'p':
                strcpy(fmtbuf, timeptr->tm_hour >= 12 ? "PM" : "AM");
                break;
            case 'r':
                temp = strftime(fmtbuf, MAX_FMT_LEN, _12HR_FORMAT, timeptr);
                if (temp == 0)
                    return 0;
                break;
            case 'R':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d:%02d", timeptr->tm_hour, timeptr->tm_min);
                break;
            case 's':
                snprintf(fmtbuf, MAX_FMT_LEN, "%lld", mktime((struct tm *)timeptr));
                break;
            case 'S':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", timeptr->tm_sec);
                break;
            case 't':
                strcpy(fmtbuf, "\t");
                break;
            case 'T':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d:%02d:%02d", timeptr->tm_hour, timeptr->tm_min, timeptr->tm_sec);
                break;
            case 'u':
                snprintf(fmtbuf, MAX_FMT_LEN, "%d", (timeptr->tm_wday + 6) % 7);
                break;
            case 'U':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", (timeptr->tm_yday + 7 - timeptr->tm_wday) / 7);
                break;
            case 'V':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", week_of_iso_year(timeptr));
                break;
            case 'w':
                snprintf(fmtbuf, MAX_FMT_LEN, "%d", timeptr->tm_wday);
                break;
            case 'W':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", (timeptr->tm_yday + 7 - (timeptr->tm_wday + 6) % 7) / 7);
                break;
            case 'x':
                temp = strftime(fmtbuf, MAX_FMT_LEN, PREFERRED_DATE, timeptr);
                if (temp == 0)
                    return 0;
                break;
            case 'X':
                temp = strftime(fmtbuf, MAX_FMT_LEN, PREFERRED_TIME, timeptr);
                if (temp == 0)
                    return 0;
                break;
            case 'y':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", abs(timeptr->tm_year % 100));
                break;
            case 'Y':
                snprintf(fmtbuf, MAX_FMT_LEN, "%02d", abs(timeptr->tm_year + 1900));
                break;
            case 'z':
                strcpy(fmtbuf, timeptr->tm_isdst ? "+0100" : "+0000");
                break;
            case 'Z':
                strcpy(fmtbuf, "GMT");
                break;
            case '%':
                strcpy(fmtbuf, "%");
                break;
            default:
                ___set_errno(EINVAL);
                return 0;
        }

        size_t fmtlen = strlen(fmtbuf);

        // 1 for null, 1 for the sometimes used '+' in years cuz I'm too lazy to do it properly
        size_t needed_size = fmtlen > cmd.padding ? fmtlen : cmd.padding;
        if (maxsize - i < needed_size + 2)
            break;

        if (cmd.padding)
            memset(s, cmd.zero_pad || cmd.sign_pad ? '0' : ' ', cmd.padding);
            memcpy(s + needed_size - fmtlen, fmtbuf, fmtlen);

        switch (cmd.conv_specifier) {
            case 'C':
            case 'F':
            case 'G':
            case 'Y':
                if (!cmd.sign_pad)
                    break;
                if (*s != '0') {
                    memmove(s + 1, s, needed_size);
                    needed_size++;
                }

                *s = timeptr->tm_year + 1900 >= 0 ? '+' : '-';
            default:
                break;

        }

        i += needed_size;
        s += needed_size;
        format += cmd.shift;
    }
    if (i >= maxsize && *format) {
        ___set_errno(ERANGE);
        return 0;
    }
    return i;
}