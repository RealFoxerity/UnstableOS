#include "include/string.h"
#include "include/ctype.h"
#include <stddef.h>
#include <stdint.h>

int atoi(const char * nptr) {
    int out = 0;
    unsigned long off = 0;
    char negative = 0; // even though -0 should be a thing, somehow it doesn't work
    if (nptr[off] == '-') {
        off++;
        negative = 1;
    }
    while (nptr[off] != '\0' && isdigit(nptr[off])) {

        out *= 10;
        out += nptr[off] - '0';
        off++;
    }
    if (negative) out *= -1; 
    return out;
}
void itoad(uint32_t i, char * out) {
    char temp;
    int ctr = 0;
    if (i & 0x80000000) { // top bit, if set number is negative
        out[0] = '-';
        out++;
        i *= -1;
    }
    for (; ; ctr++) {
        out[ctr] = '0' + i % 10;
        i /= 10;
        if (i == 0) break;
    }

    for (int i = 0; i <= ctr/2; i++) {
        temp = out[i];
        out[i] = out[ctr-i];
        out[ctr-i] = temp; 
    }
    out[ctr+1] = '\0';
}

static inline char get_nibble(char value) {
    if (value >= 10) return 'A' + value - 10;
    else return '0' + value;
}

void itoax(uint32_t i, char * out) {
    for (int j = 0; j < 8; j++) {
        out[7-j] = get_nibble((i>>(j*4))&0xF);
    }
}
void ctoax(uint8_t i, char * out) {
    out[0] = get_nibble(i >> 4);
    out[1] = get_nibble(i&0xF);
}
void stoax(uint16_t i, char * out) {
    out[0] = get_nibble((i >> 12)&0xF);
    out[1] = get_nibble((i >> 8)&0xF);
    out[2] = get_nibble((i >> 4)&0xF);
    out[3] = get_nibble(i&0xF);
}
//void i64toad(uint64_t i, char * out) { // division for 64 bit isn't something gcc can do for us on 32 bit
//    char temp;
//    int ctr = 0;
//    for (ctr = 0; ; ctr++) {
//        out[ctr] = '0' + i % 10;
//        i /= 10;
//        if (i == 0) break;
//    }
//
//    for (int i = 0; i <= ctr; i++) {
//        temp = out[i];
//        out[i] = out[ctr-i];
//        out[ctr-i] = temp; 
//    }
//    out[ctr+1] = 0;
//}

void i64toax(uint64_t i, char * out) {
    for (int j = 0; j < 16; j++) {
        out[15-j] = get_nibble((i>>(j*4))&0xF);
    }
}


size_t strlen(const char * s) {
    size_t len = 0;
    while (s[len++] != '\0');
    return len-1; // don't count the null byte
}

void memcpy(void *__restrict dest, const void *restrict src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        ((char*)dest)[i] = ((char*)src)[i];
    }
}

void memset(void * s, char c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        ((char*)s)[i] = c;
    }
}

void * memmove(void * dest, const void * src, size_t n) {
    if (src == dest) return dest;
    if (dest > src + n || dest + n < src) { // faster to do normal memcpy
        memcpy(dest, src, n);
        return dest;
    }

    unsigned char temp;
    if (src > dest) {
        for (size_t i = 0; i < n; i++) {
            temp = ((unsigned char*)src)[i];
            ((unsigned char*)dest)[i] = temp;
        }
    } else {
        for (size_t i = n-1; i > 0; i--) { // 0-1 = 1<<32
            temp = ((unsigned char*)src)[i];
            ((unsigned char*)dest)[i] = temp;
        }
        temp = ((unsigned char*)src)[0];
        ((unsigned char*)dest)[0] = temp;
    }

    return dest;
}

void strcpy(char *__restrict dest, const char *__restrict src) {
    size_t i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = src[i]; // null byte
}

char memcmp(const void *s1, const void *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (((char*)s1)[i] != ((char*)s2)[i]) {
            if (((char*)s1)[i] < ((char*)s2)[i]) return -1;
            else return 1;
        }
    }
    return 0;
}

char strcmp(const char * s1, const char * s2) {
    for (size_t i = 0; s1[i] != '\0' && s2[i] != '\0'; i++) {
        if (((char*)s1)[i] != ((char*)s2)[i]) {
            if (((char*)s1)[i] < ((char*)s2)[i]) return -1;
            else return 1;
        }
    }
    return 0;
}

char * strchr(const char * s, int c) {
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == c) return (char*)s+i;
    }
    return NULL;
}

char * strchrnul(const char * s, int c) {
    char * out = strchr(s, c);
    return out == NULL?((char*)s+strlen(s)):out;
}

char * strrchr(const char * s, int c) {
    if (strlen(s) == 0) return NULL;
    for (int i = strlen(s)-1; i >= 0; i--) {
        if (s[i] == c) return (char*)s+i;
    }
    return NULL;
}