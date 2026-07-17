#ifndef _ERRNO_H
#define _ERRNO_H

extern __thread int errno; // libc_init.c
void ___set_errno(int error);
int ___get_errno();
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
#define ENODEV    26
#define ENOTTY    27
#define EPERM     28
#define EEXIST    29
#define ENXIO     30
#define EACCES    31
#define ETIMEDOUT 32
#define EOWNERDEAD 33
#define ENOTRECOVERABLE 34
#define EDEADLK   35
#define ENOTSUP   36
#define ENAMETOOLONG 37
#define EILSEQ    38
#define ENOSPC    39
#define ENOTEMPTY 40
#define EXDEV     41
#endif