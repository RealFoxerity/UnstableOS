#ifndef DIRENT_H
#define DIRENT_H
#include <stddef.h>
#include "stdio.h"

#define IFTODT(mode) (mode >> 12)
#define DTTOIF(dirtype) (dirtype << 12)
#define DT_UNKNOWN 0
#define DT_REG IFTODT(__ITMODE_REG)
#define DT_DIR IFTODT(__ITMODE_DIR)
#define DT_BLK IFTODT(__ITMODE_BLK)
#define DT_CHR IFTODT(__ITMODE_CHAR)
/*
#define DT_FIFO 4
#define DT_LNK 5
#define DT_SOCK 6
*/

typedef size_t ino_t;

struct dirent {
    ino_t d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];  
};

struct {
    int fd;
    size_t dent_size;
    struct dirent dent;
} typedef DIR;

DIR * fdopendir(int fd);
DIR * opendir(const char * filename);
int dirfd(DIR * dirp);
int closedir(DIR * dirp);

struct dirent * readdir(DIR * dirp);
void rewinddir(DIR * dirp);
void seekdir(DIR * dirp, off_t loc);
off_t telldir(DIR * dirp);

#endif