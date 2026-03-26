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
};


// our defines, more readable names
#define __ITMODE_MASK       0xF000
#define __ITMODE_REG        0x1000
#define __ITMODE_DIR        0x2000
#define __ITMODE_PIPE       0x3000
#define __ITMODE_BLK        0x4000
#define __ITMODE_CHAR       0x8000

#define __IPMODE_MASK       0x0FFF
#define __IPMODE_O_STICKY   0x0008 // sticky bit
#define __IPMODE_O_READ     0x0004
#define __IPMODE_O_WRITE    0x0002
#define __IPMODE_O_EXEC     0x0001

#define __IPMODE_G_SET      0x0080 // SGID
#define __IPMODE_G_READ     0x0040
#define __IPMODE_G_WRITE    0x0020
#define __IPMODE_G_EXEC     0x0010

#define __IPMODE_U_SET      0x0800 // SUID
#define __IPMODE_U_READ     0x0400
#define __IPMODE_U_WRITE    0x0200
#define __IPMODE_U_EXEC     0x0100

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

#endif