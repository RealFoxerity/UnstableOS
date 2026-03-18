#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include "sys/types.h"

#define PRINTF_MAX_FORMAT_OUT 128

#define EOF 0

void vfprintf(int fd, const char * format, va_list args);
void __attribute__((format(printf, 2, 3))) fprintf(int fd, const char * format, ...);

void vsprintf(char * s, const char * format, va_list args);
void __attribute__((format(printf, 2, 3))) sprintf(char * s, const char * format, ...);

void __attribute__((format(printf, 1, 2))) printf(const char * format, ...);

int __attribute__((format(scanf, 1, 2))) scanf(const char * format, ...);
int __attribute__((format(scanf, 2, 3))) fscanf(int fd, const char * format, ...);

int vscanf(const char * format, va_list args);
int vfscanf(int fd, const char * format, va_list args);

int __attribute__((format(scanf, 2, 3))) sscanf(const char * s, const char * format, ...);
int vsscanf(const char * s, const char * format, va_list args);

int getc(int fd);
int getchar();

int putc(int c, int fd);
int putchar(int c);

char * fgets(char * s, int size, int fd);


#endif