#include "fat_structs.h"
#include "fat_internal.h"
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <time.h>

#include "kernel.h"
#include "fs/fs.h"

// intentionally missing ".", that's handled separately
#define FAT_NAME_BLACKLIST " \"*/:<>?\\|+,;=[]\x7f\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"

long fat_name_to_short(const char *pathname, char shortname[11]) {
    if (!pathname)
        return -EFAULT;

    size_t len = strnlen(pathname, 13);
    if (len > 12)
        return -ENAMETOOLONG;

    if (strpbrk(pathname, FAT_NAME_BLACKLIST))
        return -EILSEQ;

    const char * dot = strchr(pathname, '.');
    if (dot == NULL && len > 8)
        return -ENAMETOOLONG;

    if (dot) {
        if (*(dot+1) == '\0')
            return -EILSEQ;

        if (strchr(dot + 1, '.') != NULL)
            return -EILSEQ;

        if (strlen(dot+1) > 3)
            return -EILSEQ;
    }

    memset(shortname, ' ', 11);
    if (!dot) {
        memcpy(shortname, pathname, strlen(pathname));
    } else {
        memcpy(shortname, pathname, dot - pathname);
        // can't use strcpy() as that would set the null byte
        memcpy(shortname + 8, dot + 1, strlen(dot));
    }

    for (int i = 0; i < 11; i++) {
        shortname[i] = toupper(shortname[i]);
    }

    if ((unsigned char)shortname[0] == FAT_DIR_FREE) shortname[0] = '\x05';
    return 0;
}

void fat_short_to_name(const char shortname[11], char out[13]) {
    // can't use stuff like strchr because shortname might not be null terminated
    for (int i = 0; i < 8; i++) {
        if (shortname[i] == ' ')
            break;
        if (i == 0 && shortname[i] == '\x05')
            *out++ = '\xe5';
        else
            *out++ = shortname[i];
    }
    if (shortname[8] == ' ') {
        *out = '\0';
        return;
    }
    *out++ = '.';
    for (int i = 0; i < 3; i++) {
        if (shortname[8 + i] == ' ')
            break;
        *out++ = shortname[8 + i];
    }
    *out = '\0';
}

time_t fat_time_to_epoch(struct fat_time ft, struct fat_date fd) {
    struct tm tm = {
        .tm_year = fd.year + 80, // fat time is since 1980, tm from 1900 because obviously
        .tm_hour = ft.hours,
        .tm_min  = ft.minutes,
        .tm_sec  = ft.seconds * 2,
    };
    static const char month_days[11] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30};
    if (fd.month > 12)
        fd.month = 12;
    if (fd.month == 0)
        fd.month = 1;

    int yday = 0;
    for (int i = 0; i < fd.month - 1; i++)
        yday += month_days[i];
    yday += fd.day;
    tm.tm_yday = yday;
    return mktime(&tm);
}