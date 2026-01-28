#ifndef STRING_H
#define STRING_H
#include <stddef.h>
#include <stdint.h>
int atoi(const char * nptr);

void itoad(uint32_t i, char * out); // signed int
void itoaud(uint32_t i, char * out); // unsigned int

void ctoax(uint8_t i, char * out);
void itoax(uint32_t i, char * out);
void stoax(uint16_t i, char * out);
void i64toax(uint64_t i, char * out);

size_t strlen(const char * s);
void memcpy(void *__restrict dest, const void *__restrict src, size_t n);
void * memmove(void * dest, const void * src, size_t n);
void strcpy(char *__restrict dest, const char *__restrict src);
char memcmp(const void *s1, const void *s2, size_t n);
void memset(void * s, char c, size_t n);
char strcmp(const char * s1, const char * s2);
char strncmp(const char * s1, const char * s2, size_t n);
char * strchr(const char * s, int c);
char * strchrnul(const char * s, int c);
char * strrchr(const char * s, int c);

#endif