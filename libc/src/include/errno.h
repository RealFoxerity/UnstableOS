#ifndef _ERRNO_H
#define _ERRNO_H

extern int errno; // libc_init.c

#define ENOSYS    1
#define EINVAL    2
#define ERANGE    3
#define ENOLCK    4
#define ESRCH     5
#define EFAULT    6
#define EBADF     7
#define ENOENT    8
#define EBUSY     9
#define EPIPE     10
#define EIO       11
#define EAGAIN    12
#define E2BIG     13 // syscalls return ssize_t, in case we'd need to read/write >2GB at once, send E2BIG
#define ESPIPE    14
#define EMFILE    15 // process fd limit reached
#define ENFILE    16 // system fd limit reached
#define EFBIG     17 // write offset exceeds maximum file size in files that cannot be grown, note that offset equal to file size returns EOF
#define ENOTDIR   18
#define EISDIR    19
#define ENOMEM    20
#define EROFS     21
#define ENOEXEC   22
#define ECHILD    23
#define EOVERFLOW 24
#define EINTR     25
#endif