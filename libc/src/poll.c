#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <UnstableOS/syscalls.h>
#include <errno.h>

int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
    return ppoll(fds, nfds,
        timeout == -1 ?
            NULL :
            &(struct timespec){.tv_sec = timeout / 1000, .tv_nsec = (timeout % 1000) * 1000000},
        NULL);
}
int ppoll(struct pollfd fds[], nfds_t nfds,
    const struct timespec *restrict timeout,
    const sigset_t *restrict sigmask) {
    int ret = syscall(SYSCALL_PPOLL, fds, nfds, timeout, sigmask);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}