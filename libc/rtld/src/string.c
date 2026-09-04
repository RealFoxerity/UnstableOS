#include <stddef.h>
#include "string.h"
size_t strlen(const char * s) {
    const char * end = s;
    unsigned long n = (unsigned long)-1;
    asm volatile (
        "repnz scasb"
        : "+D"(end), "+c"(n)
        : "a"(0)
        : "memory"
    );
    return end - s - 1;
}
size_t strnlen(const char * s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\0') return i;
    }
    return n;
}

int strcmp(const char * s1, const char * s2) {
    for (size_t i = 0; s1[i] != '\0' || s2[i] != '\0'; i++) {
        if (((char*)s1)[i] != ((char*)s2)[i]) {
            if (((char*)s1)[i] < ((char*)s2)[i]) return -1;
            return 1;
        }
    }
    return 0;
}

int strncmp(const char * s1, const char * s2, size_t n) {
    for (size_t i = 0; i < n && (s1[i] != '\0' || s2[i] != '\0'); i++) {
        if (((char*)s1)[i] != ((char*)s2)[i]) {
            if (((char*)s1)[i] < ((char*)s2)[i]) return -1;
            return 1;
        }
    }
    return 0;
}
char * strcpy(char *__restrict dest, const char *__restrict src) {
    return memcpy(dest, src, strlen(src) + 1);
}
char * strncpy(char *__restrict dest, const char *__restrict src, size_t dsize) {
    size_t i = 0;
    for (i = 0; i < dsize && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    memset(dest + i, 0, dsize - i);
    return dest;
}

char * strchrnul(const char * s, int c) {
    for (;*s != '\0' && *s != c; s++) {}
    return (char*)s;
}

char * strchr(const char * s, int c) {
    char * n = strchrnul(s, c);
    if (*n == '\0')
        return NULL;
    return n;
}
char * strrchr(const char * s, int c) {
    if (strlen(s) == 0) return NULL;
    for (int i = strlen(s)-1; i >= 0; i--) {
        if (s[i] == c) return (char*)s+i;
    }
    return NULL;
}
