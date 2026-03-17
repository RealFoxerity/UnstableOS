#ifndef _FCNTL_H
#define _FCNTL_H

#define O_RDONLY 1
#define O_WRONLY 2
#define O_RDWR 3
#define O_CREAT 4 // not yet implemented
#define O_TRUNC 8 // not yet implemented
#define O_DIRECTORY 16 // open() fails with ENOTDIR if resolved to a regular file

#define AT_FDCWD -1
#include "sys/types.h"

int open(const char * path, unsigned short flags, mode_t mode);
int openat(int fd, const char * path, unsigned short flags, mode_t mode);
int creat(const char * path, mode_t mode);

#endif