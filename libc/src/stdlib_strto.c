#include "include/stdlib.h"
#include "include/ctype.h"

// unfortunately we don't support bases, because i'm too lazy :3

unsigned long long strtoull(const char * start, char ** end_out) {
    unsigned long long val = 0;
    char * end = (char*)start;
    while (isdigit(*end)) {
        val *= 10;
        val += *end - '0';
        end ++;
    }

    if (end_out) *end_out = end;
    return val;
}

unsigned long strtoul(const char * start, char ** end_out) {
    char * end;
    unsigned long long val = strtoull(start, &end);
    if (end_out) *end_out = end; 
    return (unsigned long) val;
}

long long strtoll(const char * start, char ** end_out) {
    long long val = 0;
    char negative = 0; // has to be negative because we can't do -0
    char * end = (char*)start;
    if (*end == '-') {
        end++;
        negative = 1;
    }
    while (isdigit(*end)) {
        val *= 10;
        val += *end - '0';
        end ++;
    }

    if (end_out) *end_out = end;
    return negative ? -val : val;
}

long strtol(const char * start, char ** end_out) {
    char * end;
    long long val = strtoll(start, &end);
    if (end_out) *end_out = end; 
    return (long) val;
}