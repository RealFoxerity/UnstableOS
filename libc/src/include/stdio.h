#ifndef STDIO_H
#define STDIO_H

#include <stddef.h>
#include <stdarg.h>
#define ssize_t long // normally in <sys/types.h> which is not available to us

#define PRINTF_MAX_FORMAT_OUT 128 

#define STDIN 0
#define STDOUT 1
#define STDERR 2

void vfprintf(int fd, const char * format, va_list args);
void fprintf(int fd, const char * format, ...);

void vsprintf(char * s, const char * format, va_list args);
void sprintf(char * s, const char * format, ...);

void printf(const char * format, ...);

ssize_t write(int fd, const void * buf, size_t count);
ssize_t read (int fd, void * buf, size_t count);

int dup(int fd);
int dup2(int oldfd, int newfd);

#endif