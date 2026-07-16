#ifndef _FCNTL_H
#define _FCNTL_H

// file access modes; get with F_GETFL, cannot be set after open()
#define O_RDONLY 0x1
#define O_WRONLY 0x2
#define O_RDWR   0x3
#define O_SEARCH 0x4      // not yet fully implemented
#define O_EXEC   O_SEARCH // not yet implemented, POSIX says these 2 can have the same values
#define O_ACCMODE 0x7     // bitmask for the access modes

// file creation flags
#define O_CREAT     0x8   // not yet implemented
#define O_TRUNC     0x10  // not yet implemented
#define O_DIRECTORY 0x20
#define O_EXCL      0x40  // not yet implemented
#define O_NOCTTY    0x80  // not yet implemented
#define O_NOFOLLOW  0x100 // not yet implemented
#define O_TTY_INIT  0x200 // not yet implemented

// file status flags; get with F_GETFL, set with F_SETFL
#define O_SYNC      0x400
#define O_APPEND    0x800

// file descriptor flags; get with F_GETFD, set with F_SETFD
#define O_CLOEXEC   0x1000
#define O_CLOFORK   0x2000

// strictly use just as a reference point,
// when passed to openat_inode, won't resolve the final mountpoint
#define O_PATH      0x4000


#define AT_FDCWD (-1)
#define AT_REMOVEDIR 1
#include "sys/types.h"
#include "sys/stat.h"

int open(const char * path, unsigned short flags, ...);
int openat(int fd, const char * path, unsigned short flags, ...);
int creat(const char * path, mode_t mode);
int mkdirat(int fd, const char *path, mode_t mode);

#define F_DUPFD         1
#define F_DUPFD_CLOEXEC 2
#define F_DUPFD_CLOFORK 3
#define F_GETFD         4
#define F_SETFD         5
#define F_GETFL         6
#define F_SETFL         7
#define F_GETOWN        8  // not implemented - no sockets
#define F_SETOWN        9  // not implemented - no sockets
#define F_GETOWN_EX     10 // not implemented - no sockets
#define F_SETOWN_EX     11 // not implemented - no sockets

#define F_GETLK         12 // not implemented - no locks
#define F_SETLK         13 // not implemented - no locks
#define F_SETLKW        14 // not implemented - no locks
#define F_OFD_GETLK     15 // not implemented - no locks
#define F_OFD_SETLKW    16 // not implemented - no locks

#define FD_CLOEXEC O_CLOEXEC
#define FD_CLOFORK O_CLOFORK

int fcntl(int fildes, int cmd, ...);
int unlinkat(int fd, const char *path, int flag);

#endif