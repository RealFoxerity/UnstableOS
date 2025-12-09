#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H
#include <stdint.h>
#include <stddef.h>

int open(const char * path, int flags, int mode);
int close(int fd);

struct {

} typedef DIR;

struct dirent {
    
};

DIR * opendir(const char * path);
struct dirent * readdir(DIR * dirp);

size_t read(int fd, void * buffer, size_t count);
size_t write(int fd, void * buffer, size_t count);


#endif