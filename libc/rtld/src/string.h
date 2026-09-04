#ifndef STRING_H
#define STRING_H
#include <stddef.h>
size_t strlen(const char * s);
size_t strnlen(const char * s, size_t n);
int strcmp(const char * s1, const char * s2);
int strncmp(const char * s1, const char * s2, size_t n);

void * memset(void * s, int c, size_t n);
void * memcpy(void *__restrict dest, const void *__restrict src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

char * strcpy(char *__restrict dest, const char *__restrict src);
char * strncpy(char *__restrict dest, const char *__restrict src, size_t dsize);
char * strchrnul(const char * s, int c);
char * strchr(const char * s, int c);
char * strrchr(const char * s, int c);
#endif