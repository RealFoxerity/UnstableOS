#include "errno.h"
#include "include/stdlib.h"
#include "include/ctype.h"
#include <stddef.h>
#include <string.h>
#include <limits.h>


static char _strto_get_value(char c) {
    if (isdigit(c)) return c - '0';
    if (!isalpha(c)) return -1;
    return tolower(c) - 'a' + 10;
}

unsigned long long strtoull(const char * restrict start, char ** restrict end_out, int base) {
    const char * iter = start;
    if (base < 0 || base > 36 || base == 1 || start == NULL || *start == '\0') {
        invalid_arg:
        ___set_errno(EINVAL);
        if (end_out != NULL) *end_out = (char *)start;
        return 0;
    }
    while (isspace(*iter)) iter ++;
    if (*iter == '\0') goto invalid_arg;

    char neg = 0;
    switch (*iter) {
        case '-':
            neg = 1;
            iter++;
            break;
        case '+':
            neg = 0;
            iter++;
            break;
    }

    if (base == 0) {
        if (!isdigit(*iter)) goto invalid_arg;
        if (*iter != '0') {
            base = 10;
        } else {
            iter ++;
            if (tolower(*iter) == 'x') {
                iter ++;
                base = 16;
            } else
                base = 8;
        }
    }

    if (base == 16 && (strcmp(iter, "0x") == 0 || strcmp(iter, "0X") == 0)) iter += 2;

    unsigned long long acc = 0;
    while (isalnum(*iter)) {
        char val = _strto_get_value(*iter);

        if (val == -1 || val >= base) break;
        if (acc * base + val < acc) { // overflow
            acc = ULLONG_MAX;
            ___set_errno(ERANGE);
        } else {
            acc *= base;
            acc += val;
        }
        iter ++;
    }

    if (end_out != NULL) *end_out = (char *)iter;
    return acc * (neg ? -1 : 1); // what does this mean in an unsigned context?
}

unsigned long strtoul(const char * restrict start, char ** restrict end_out, int base) {
    unsigned long long val = strtoull(start, end_out, base);
    if (val > ULONG_MAX) {
        ___set_errno(ERANGE);
        return ULONG_MAX;
    }
    return (unsigned long) val;
}

long long strtoll(const char * restrict start, char ** restrict end_out, int base) {
    const char * iter = start;
    if (base < 0 || base > 36 || base == 1 || start == NULL || *start == '\0') {
        invalid_arg:
        ___set_errno(EINVAL);
        if (end_out != NULL) *end_out = (char *)start;
        return 0;
    }
    while (isspace(*iter)) iter ++;
    if (*iter == '\0') goto invalid_arg;

    char neg = 0;
    switch (*iter) {
        case '-':
            neg = 1;
            iter++;
            break;
        case '+':
            neg = 0;
            iter++;
            break;
    }

    if (base == 0) {
        if (!isdigit(*iter)) goto invalid_arg;
        if (*iter != '0') {
            base = 10;
        } else {
            iter ++;
            if (tolower(*iter) == 'x') {
                iter ++;
                base = 16;
            } else
                base = 8;
        }
    }

    if (base == 16 && (strcmp(iter, "0x") == 0 || strcmp(iter, "0X") == 0)) iter += 2;

    long long acc = 0;
    while (isalnum(*iter)) {
        char val = _strto_get_value(*iter);

        if (val == -1 || val >= base) break;
        if (acc * base + val < acc) { // overflow
            acc = LLONG_MAX;
            ___set_errno(ERANGE);
        } else {
            acc *= base;
            acc += val;
        }
        iter ++;
    }

    if (end_out != NULL) *end_out = (char *)iter;
    return acc * (neg ? -1 : 1);
}

long strtol(const char * restrict start, char ** restrict end_out, int base) {
    long long val = strtoll(start, end_out, base);
    if (val > LONG_MAX || val < LONG_MIN) {
        ___set_errno(ERANGE);
        if (val > LONG_MAX) return LONG_MAX;
        return LONG_MIN;
    }
    return (long) val;
}