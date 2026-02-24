#ifndef STDIO_H
#define STDIO_H

#include <stddef.h>
#include <stdarg.h>
#define ssize_t long // normally in <sys/types.h> which is not available to us
typedef ssize_t off_t; // TODO: when finally implementing errno, change to size_t instead of ssize_t

#define PRINTF_MAX_FORMAT_OUT 128 

#define EOF 0

#define STDIN 0
#define STDOUT 1
#define STDERR 2

#define __ITMODE_MASK       0xF000
#define __ITMODE_REG        0x1000
#define __ITMODE_DIR        0x2000
#define __ITMODE_BLK        0x4000
#define __ITMODE_CHAR       0x8000

#define __IPMODE_MASK       0x0FFF
#define __IPMODE_O_READ     0x0004
#define __IPMODE_O_WRITE    0x0002
#define __IPMODE_O_EXEC     0x0001

#define __IPMODE_G_READ     0x0040
#define __IPMODE_G_WRITE    0x0020
#define __IPMODE_G_EXEC     0x0010

#define __IPMODE_U_READ     0x0400
#define __IPMODE_U_WRITE    0x0200
#define __IPMODE_U_EXEC     0x0100

// the classic posix macros
#define S_IRUSR __IPMODE_U_READ
#define S_IWUSR __IPMODE_U_WRITE
#define S_IXUSR __IPMODE_U_EXEC
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)

#define S_IRGRP __IPMODE_G_READ
#define S_IWGRP __IPMODE_G_WRITE
#define S_IXGRP __IPMODE_G_EXEC
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)

#define S_IROTH __IPMODE_O_READ
#define S_IWOTH __IPMODE_O_WRITE
#define S_IXOTH __IPMODE_O_EXEC
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)


#define I_ISREG(mode) (((mode) & __ITMODE_MASK) == __ITMODE_REG)
#define I_ISDIR(mode) (((mode) & __ITMODE_MASK) == __ITMODE_DIR)
#define I_ISBLK(mode) (((mode) & __ITMODE_MASK) == __ITMODE_BLK)
#define I_ISCHAR(mode) (((mode) & __ITMODE_MASK) == __ITMODE_CHAR)

#define O_RDONLY 1
#define O_WRONLY 2
#define O_RDWR 3
#define O_CREAT 4 // not yet implemented
#define O_TRUNC 8 // not yet implemented 

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

char * fgets(char * s, int size, int fd);

int open(const char * path, unsigned short flags, unsigned short mode);
ssize_t write(int fd, const void * buf, size_t count);
ssize_t read (int fd, void * buf, size_t count);

off_t seek(int fd, off_t offset, int whence);

int dup(int fd);
int dup2(int oldfd, int newfd);

#endif