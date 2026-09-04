#ifndef FILES_H
#define FILES_H

#define O_RDONLY 0x1
#define O_WRONLY 0x2
#define O_RDWR   0x3
#define O_DIRECTORY 0x20

#define AT_FDCWD (-1)

int openat(int fd, const char * path, unsigned short flags);
int close(int fd);
long pread(int fd, void * buf, unsigned long count, unsigned long long offset);

int openelf(const char * name, const char * runpath);

#endif