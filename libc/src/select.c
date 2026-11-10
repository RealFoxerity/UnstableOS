#include <sys/select.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <stddef.h>
#include <errno.h>

int pselect(int nfds, fd_set *restrict readfds,
            fd_set *restrict writefds, fd_set *restrict errorfds,
            const struct timespec *restrict timeout,
            const sigset_t *restrict sigmask) {
    if (!readfds && !writefds && !errorfds) {
        if (!timeout) {
            sigset_t current_mask;
            if (!sigmask) {
                sigprocmask(SIG_SETMASK, NULL, &current_mask);
                sigmask = &current_mask;
            }
            return sigsuspend(sigmask);
        }
        return clock_nanosleep(CLOCK_MONOTONIC, 0, timeout, NULL);
    }

    if (nfds < 0 || nfds > FD_SETSIZE) {
        ___set_errno(EINVAL);
        return -1;
    }

    struct pollfd pfds[nfds];
    for (int i = 0; i < nfds; i++) {
        pfds[i].fd = i;
        pfds[i].events = 0;
        if (readfds && FD_ISSET(i, readfds))
            // using the fact POLLHUP isn't even valid in events
            pfds[i].events |= POLLIN | POLLHUP;
        if (writefds && FD_ISSET(i, writefds))
            pfds[i].events |= POLLOUT;
        if (errorfds && FD_ISSET(i, errorfds))
            // kinda bad but like how else are you going to do this?
            // using the fact POLLERR isn't even valid in events
            pfds[i].events |= POLLIN | POLLERR;
    }
    int ret = ppoll(pfds, nfds, timeout, sigmask);
    // "On failure, the objects pointed to by the readfds, writefds, and errorfds arguments shall not be modified."
    if (ret < 0)
        return -1;
    // "If the timeout interval expires ... readfds, writefds, and errorfds arguments shall have all bits set to 0."
    if (readfds)  FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (errorfds) FD_ZERO(errorfds);
    if (ret == 0) return 0;
    // this is kinda bad, EBADF should theoretically be checked before blocking
    // however, due to our dependance on ppoll (which doesn't check at all), it's not really possible
    char errored = 0;
    for (int i = 0; i < nfds; i++) {
        if (!pfds[i].events)
            continue;
        if (pfds[i].revents & POLLNVAL) {
            errored = 1;
            continue;
        }
        if (readfds && pfds[i].events  & POLLHUP && pfds[i].revents & POLLIN)
            FD_SET(i, readfds);
        if (writefds && pfds[i].events & POLLOUT && pfds[i].revents & POLLOUT)
            FD_SET(i, writefds);
        if (errorfds && pfds[i].events & POLLERR && pfds[i].revents & (POLLERR | POLLHUP))
            FD_SET(i, errorfds);
    }
    // basically the only possible reason
    if (errored)
        ___set_errno(EBADF);
    return errored ? -1 : ret;
}
int select(int nfds, fd_set *restrict readfds,
    fd_set *restrict writefds, fd_set *restrict errorfds,
    struct timeval *restrict timeout) {

    struct timespec ts;
    if (timeout) {
        ts = (struct timespec) {
            .tv_sec = timeout->tv_sec,
            .tv_nsec = timeout->tv_usec * 1000,
        };
    }
    return pselect(nfds, readfds, writefds, errorfds, timeout ? &ts : NULL, NULL);
}