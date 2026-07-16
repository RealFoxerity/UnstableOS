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


// our defines, more readable names
#define __ITMODE_MASK       070000
#define __ITMODE_REG        010000
#define __ITMODE_DIR        020000
#define __ITMODE_PIPE       030000
#define __ITMODE_BLK        040000
#define __ITMODE_CHAR       050000

#define __IPMODE_MASK       07777
#define __IPMODE_O_STICKY   01000 // sticky bit
#define __IPMODE_O_READ     00004
#define __IPMODE_O_WRITE    00002
#define __IPMODE_O_EXEC     00001

#define __IPMODE_G_SET      02000 // SGID
#define __IPMODE_G_READ     00040
#define __IPMODE_G_WRITE    00020
#define __IPMODE_G_EXEC     00010

#define __IPMODE_U_SET      04000 // SUID
#define __IPMODE_U_READ     00400
#define __IPMODE_U_WRITE    00200
#define __IPMODE_U_EXEC     00100

// the classic posix macros
#define S_IRUSR __IPMODE_U_READ
#define S_IWUSR __IPMODE_U_WRITE
#define S_IXUSR __IPMODE_U_EXEC
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#define S_ISUID __IPMODE_U_SET

#define S_IRGRP __IPMODE_G_READ
#define S_IWGRP __IPMODE_G_WRITE
#define S_IXGRP __IPMODE_G_EXEC
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_ISGID __IPMODE_G_SET

#define S_IROTH __IPMODE_O_READ
#define S_IWOTH __IPMODE_O_WRITE
#define S_IXOTH __IPMODE_O_EXEC
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#define S_ISVTX __IPMODE_O_STICKY

#define S_IFMT __ITMODE_MASK
#define S_IFBLK __ITMODE_BLK
#define S_IFCHR __ITMODE_CHAR
#define S_IFFIFO __ITMODE_PIPE
#define S_IFREG __ITMODE_REG
#define S_IFDIR __ITMODE_DIR

#define S_ISBLK(mode)  (((mode) & S_IFMT) == S_IFBLK)
#define S_ISCHR(mode)  (((mode) & S_IFMT) == S_IFCHR)
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFFIFO)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)

int stat(const char * __restrict path, struct stat * __restrict buf);
int fstat(int fd, struct stat * buf);
int fstatat(int fd, const char * __restrict path, struct stat * __restrict buf, int flags);
mode_t umask(mode_t mask);

int mkdir(const char *path, mode_t mode);
#endif