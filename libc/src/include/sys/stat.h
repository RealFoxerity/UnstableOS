#ifndef _STAT_H
#define _STAT_H

#include "types.h"
#include "../time.h"
struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;

    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;

    blksize_t st_blksize;
    blkcnt_t st_blocks;
};


#define __IPMODE_MASK 07777

#define S_IRUSR (00400)
#define S_IWUSR (00200)
#define S_IXUSR (00100)
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#define S_ISUID (04000)

#define S_IRGRP (00040)
#define S_IWGRP (00020)
#define S_IXGRP (00010)
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_ISGID (02000)

#define S_IROTH (00004)
#define S_IWOTH (00002)
#define S_IXOTH (00001)
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#define S_ISVTX (01000)

#define S_IFMT   (0170000)
#define S_IFSOCK (0140000)
#define S_IFLNK  (0120000)
#define S_IFREG  (0100000)
#define S_IFBLK  (0060000)
#define S_IFDIR  (0040000)
#define S_IFCHR  (0020000)
#define S_IFFIFO (0010000)

#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)
#define S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#define S_ISBLK(mode)  (((mode) & S_IFMT) == S_IFBLK)
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode)  (((mode) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFFIFO)

int stat(const char * __restrict path, struct stat * __restrict buf);
int fstat(int fd, struct stat * buf);
int fstatat(int fd, const char * __restrict path, struct stat * __restrict buf, int flags);
mode_t umask(mode_t mask);

int mkdir(const char *path, mode_t mode);
int mkdirat(int fd, const char *path, mode_t mode);
int mknod(const char *path, mode_t mode, dev_t dev);
int mknodat(int fd, const char *path, mode_t mode, dev_t dev);

#define UTIME_NOW  (-1)
#define UTIME_OMIT (-2)
int futimens(int fd, const struct timespec times[2]);
int utimensat(int fd, const char *path, const struct timespec times[2], int flag);
#endif