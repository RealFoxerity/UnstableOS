#ifndef ERRNO_H
#define ERRNO_H

#define ENOSYS -1
#define EINVAL -2
#define ERANGE -3
#define ENOLCK -4
#define ESRCH -5
#define EFAULT -6
#define EBADF -7
#define ENOENT -8
#define EBUSY -9
#define ENOTBLK -10
#define EPIPE -11
#define EIO -12
#define EAGAIN -13
#define E2BIG -14 // syscalls return ssize_t, in case we'd need to read/write >2GB at once, send E2BIG
#define ESPIPE -15
#define EMFILE -16 // process fd limit reached
#define ENFILE -17 // system fd limit reached
#define EFBIG -18 // write offset exceeds maximum file size in files that cannot be grown, note that offset equal to file size returns EOF
#define ENOTDIR -19
#define EISDIR -20
#define ENOMEM -21
#define EROFS -22
#define ENOEXEC -23
#endif