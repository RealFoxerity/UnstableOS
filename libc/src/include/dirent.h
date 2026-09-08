#ifndef _DIRENT_H
#define _DIRENT_H
#include <stddef.h>
#include <stdio.h>

#define IFTODT(mode) (mode >> 12)
#define DTTOIF(dirtype) (dirtype << 12)
#define DT_UNKNOWN 0
#define DT_REG IFTODT(S_IFREG)
#define DT_DIR IFTODT(S_IFDIR)
#define DT_BLK IFTODT(S_IFBLK)
#define DT_CHR IFTODT(S_IFCHR)
#define DT_FIFO IFTODT(S_IFFIFO)
#define DT_LNK IFTODT(S_IFLNK)
#define DT_SOCK IFTODT(S_IFSOCK)

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