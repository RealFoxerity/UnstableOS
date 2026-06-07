#include "include/string.h"
#include "include/stdlib.h"
#include "include/ctype.h"
#include <stddef.h>
#include <stdint.h>

#include <errno.h>

long long atoll(const char * nptr) {
    int old_errno = errno;
    errno = 0;
    long long out = strtoll(nptr, NULL, 0);
    if (errno != 0) {
        errno = old_errno;
        return 0;
    }
    errno = old_errno;
    return out;
}

long atol(const char * nptr) {
    return (long)atoll(nptr);
}
int atoi(const char * nptr) {
    return (int)atoll(nptr);
}

void itoad(uint64_t num, char * out) {
    char temp;
    int ctr = 0;
    if (num & (uint64_t)1 << 63) { // top bit, if set number is negative
        out[0] = '-';
        out++;
        num *= -1;
    }
    for (; ; ctr++) {
        out[ctr] = '0' + num % 10;
        num /= 10;
        if (num == 0) break;
    }

    for (int i = 0; i <= ctr/2; i++) {
        temp = out[i];
        out[i] = out[ctr-i];
        out[ctr-i] = temp;
    }
    out[ctr+1] = '\0';
}

void itoaud(uint64_t num, char * out) {
    char temp;
    int ctr = 0;
    for (; ; ctr++) {
        out[ctr] = '0' + num % 10;
        num /= 10;
        if (num == 0) break;
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

void itoax(uint32_t num, char * out) {
    char temp;
    int ctr = 0;
    for (; ; ctr++) {
        out[ctr] = get_nibble(num % 16);
        num /= 16;
        if (num == 0) break;
    }

    for (int i = 0; i <= ctr/2; i++) {
        temp = out[i];
        out[i] = out[ctr-i];
        out[ctr-i] = temp;
    }
    out[ctr+1] = '\0';
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

size_t strnlen(const char * s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\0') return i;
    }
    return n;
}
/*
void * memcpy(void *__restrict dest, const void *__restrict src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        ((char*)dest)[i] = ((char*)src)[i];
    }
    return dest;
}
*/
void * mempcpy(void *__restrict dest, const void *__restrict src, size_t n) {
    memcpy(dest, src, n);
    return dest + n;
}

/*
void * memset(void * s, char c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        ((char*)s)[i] = c;
    }
    return s;
}
*/
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


// extremely important function, do not remove
void* memfrob(void* s, size_t n)
{
    while (n-- > 0) *(unsigned char*)s++ ^= 42;
    return s;
}

char * strncpy(char *__restrict dest, const char *__restrict src, size_t dsize) {
    size_t i = 0;
    for (i = 0; i < dsize && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    memset(dest + i, 0, dsize - i);
    return dest;
}

char * strcpy(char *__restrict dest, const char *__restrict src) {
    return strncpy(dest, src, strlen(src) + 1);
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
    for (size_t i = 0; s1[i] != '\0' || s2[i] != '\0'; i++) {
        if (((char*)s1)[i] != ((char*)s2)[i]) {
            if (((char*)s1)[i] < ((char*)s2)[i]) return -1;
            else return 1;
        }
    }
    return 0;
}

char strncmp(const char * s1, const char * s2, size_t n) {
    for (size_t i = 0; i < n && (s1[i] != '\0' || s2[i] != '\0'); i++) {
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

char * strndup(const char * s, size_t n) {
    // POSIX allows both, strnlen is slower
    //char * new = malloc(strnlen(s, n) + 1);
    char * new = malloc(n + 1);
    if (new == NULL) return NULL;
    strncpy(new, s, n);
    new[strnlen(s, n)] = '\0';
    return new;
}

char * strdup(const char * s) {
    return strndup(s, strlen(s));
}


char * strtok(char * __restrict src, const char * __restrict delim) {
    static char * last_tok = NULL;
    if (src != NULL) last_tok = src;
    if (last_tok == NULL) return NULL;

    // nothing more to parse
    if (*last_tok == '\0') return NULL;

    // find the next token
    for (size_t i = 0; last_tok[i] != '\0'; i++) {
        for (int j = 0; delim[j] != '\0'; j++) {
            if (last_tok[i] == delim[j]) goto fail;
        }
        last_tok += i;
        break;
        fail:
        continue;
    }

    // end the next token
    size_t i = 0;
    for (i = 0; last_tok[i] != '\0'; i++) {
        for (int j = 0; delim[j] != '\0'; j++) {
            if (last_tok[i] == delim[j]) goto found;
        }
    }
    found:
    last_tok[i] = '\0';

    char * ret = last_tok;
    last_tok += i + 1;
    return ret;
}
