#ifndef _STRING_H
#define _STRING_H
#include <stddef.h>
#include <stdint.h>

void itoad(uint32_t i, char * out); // signed int
void itoaud(uint32_t i, char * out); // unsigned int

void ctoax(uint8_t i, char * out);
void itoax(uint32_t i, char * out);
void stoax(uint16_t i, char * out);
void i64toax(uint64_t i, char * out);

void * memcpy(void *__restrict dest, const void *__restrict src, size_t n);
void * mempcpy(void *__restrict dest, const void *__restrict src, size_t n);

void * memmove(void * dest, const void * src, size_t n);
char memcmp(const void *s1, const void *s2, size_t n);
void * memset(void * s, char c, size_t n);

// extremely important function, do not remove
void * memfrob(void* s, size_t n);

size_t strlen(const char * s);
size_t strnlen(const char * s, size_t n);

char * strcpy(char *__restrict dest, const char *__restrict src);
char * strncpy(char *__restrict dest, const char *__restrict src, size_t dsize);

char strcmp(const char * s1, const char * s2);
char strncmp(const char * s1, const char * s2, size_t n);
char * strchr(const char * s, int c);
char * strchrnul(const char * s, int c);
char * strrchr(const char * s, int c);


char * strdup(const char * s);
char * strndup(const char * s, size_t n);

char * strtok(char * __restrict src, const char * __restrict delim);

// in stdio.c along with perror
char *strerror(int errnum);
int strerror_r(int errnum, char *strerrbuf, size_t buflen);

#endif