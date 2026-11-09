#ifndef _POLL_H
#define _POLL_H
#include <signal.h>
#include <time.h>

#define POLLIN 1
#define POLLRDNORM POLLIN
//#define POLLRDBAND 2
//#define POLLPRI 4
#define POLLOUT 8
#define POLLWRNORM POLLOUT
//#define POLLWRBAND 16

#define POLLERR 32
#define POLLHUP 64
#define POLLNVAL 128
typedef unsigned int nfds_t;

struct pollfd {
    int   fd;
    short events;
    short revents;
};

int poll(struct pollfd fds[], nfds_t nfds, int timeout);
int ppoll(struct pollfd fds[], nfds_t nfds,
    const struct timespec *__restrict timeout,
    const sigset_t *__restrict sigmask);

#endif