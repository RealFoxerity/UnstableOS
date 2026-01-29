#include "include/ctype.h"

char isprint(char c) {
    if (c >= ' ' && c <= '~') return 1;
    return 0;
}

char tolower(char c) {
    if (isupper(c)) return c + 0x20;
    return c;
}

char toupper(char c) {
    if (islower(c)) return c - 0x20;
    return c;
}

char islower(char c) {
    if (c >= 'a' && c <= 'z') return 1;
    return 0;
}
char isupper(char c) {
    if (c >= 'A' && c <= 'Z') return 1;
    return 0;
}

char isalpha(char c) {
    if (islower(c) || isupper(c)) return 1;
    return 0;
}
char isdigit(char c) {
    if (c >= '0' && c <= '9') return 1;
    return 0;
}
char isalnum(char c) {
    if (isdigit(c) || isalpha(c)) return 1;
    return 0;
}

