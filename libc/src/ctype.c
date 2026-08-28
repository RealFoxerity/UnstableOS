#include <ctype.h>

int isprint(int c) {
    if (c >= ' ' && c <= '~') return 1;
    return 0;
}

int tolower(int c) {
    if (isupper(c)) return c + 0x20;
    return c;
}

int toupper(int c) {
    if (islower(c)) return c - 0x20;
    return c;
}

int islower(int c) {
    if (c >= 'a' && c <= 'z') return 1;
    return 0;
}
int isupper(int c) {
    if (c >= 'A' && c <= 'Z') return 1;
    return 0;
}

int isalpha(int c) {
    if (islower(c) || isupper(c)) return 1;
    return 0;
}
int isdigit(int c) {
    if (c >= '0' && c <= '9') return 1;
    return 0;
}
int isalnum(int c) {
    if (isdigit(c) || isalpha(c)) return 1;
    return 0;
}
int isspace(int c) {
    switch (c) {
        default: return 0;
        case ' ':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
        case '\v':
            return 1;
    }
}