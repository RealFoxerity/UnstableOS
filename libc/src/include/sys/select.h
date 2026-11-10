#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
#include <signal.h>
#include <time.h>

#define FD_SETSIZE 128 // same as OPEN_MAX
typedef struct {unsigned long __fds[FD_SETSIZE/sizeof(unsigned long)/8];} fd_set;
struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};
int pselect(int nfds, fd_set *__restrict readfds,
    fd_set *__restrict writefds, fd_set *__restrict errorfds,
    const struct timespec *__restrict timeout,
    const sigset_t *__restrict sigmask);
int select(int nfds, fd_set *__restrict readfds,
    fd_set *__restrict writefds, fd_set *__restrict errorfds,
    struct timeval *__restrict timeout);


#define FD_CLR(fd, fdset) ((fdset)->__fds[(fd)/(sizeof(unsigned long) * 8)] &= ~(1UL << ((fd) % (sizeof(unsigned long) * 8))))
#define FD_ISSET(fd, fdset) !!((fdset)->__fds[(fd)/(sizeof(unsigned long) * 8)] & (1UL << ((fd) % (sizeof(unsigned long) * 8))))
#define FD_SET(fd, fdset) ((fdset)->__fds[(fd)/(sizeof(unsigned long) * 8)] |= (1UL << ((fd) % (sizeof(unsigned long) * 8))))
#define FD_ZERO(fdset) do {                                 \
    size_t __i = 0;                                         \
    unsigned long *__f = (fdset)->__fds;                    \
    for (; __i < FD_SETSIZE/sizeof(unsigned long)/8; __i++) \
        *__f++ = 0;                                         \
} while (0)

#endif