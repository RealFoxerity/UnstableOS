#include "fs/fs.h"
#include "fs/vfs.h"
#include "mm/kernel_memory.h"
#include "kernel.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#include "kernel_sched_queues.h"
#include "dev_ops.h"
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <string.h>

struct pollfd_priv {
    inode_t * ino;
    short events;
    short revents;
};

int sys_ppoll(struct pollfd fds[], nfds_t nfds,
    const struct timespec *restrict timeout,
    const sigset_t *restrict sigmask
) {
    struct timespec ts = {0};
    if (timeout)
        ts = *timeout;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000)
        return -EINVAL;

    sigset_t set = 0;
    sigset_t oldset = current_thread->sa_mask;
    if (sigmask)
        set = *sigmask;
    else
        set = oldset;
    set &= SAFE_SA_MASK;

    if (!nfds)
        return 0;
    if (nfds > OPEN_MAX /*|| nfds > INT_MAX*/)
        return -EINVAL;

    struct pollfd_priv * pfds = kalloc(nfds * sizeof(struct pollfd_priv));
    if (!pfds)
        return -EAGAIN;
    memset(pfds, 0, nfds * sizeof(struct pollfd_priv));

    current_thread->sa_mask = set;
    if (check_eintr()) {
        current_thread->sa_mask = oldset;
        kfree(pfds);
        return -EINTR;
    }

    int ready = 0;
    size_t waitable_fds = 0;
    spinlock_acquire_interruptible(&current_process->lock);
    for (nfds_t i = 0; i < nfds; i++) {
        pfds[i].events = fds[i].events & (POLLIN | POLLOUT);

        // atomic so that the compiler doesn't optimize this away
        int this_fd = __atomic_load_n(&fds[i].fd, __ATOMIC_RELAXED);
        if (this_fd < 0 || this_fd >= OPEN_MAX || !current_process->fds[this_fd]) {
            pfds[i].revents = POLLNVAL;
            continue;
        }

        if (pfds[i].events & POLLIN  && !(current_process->fds[this_fd]->flags & O_RDONLY))
            pfds[i].events ^= POLLIN;
        if (pfds[i].events & POLLOUT && !(current_process->fds[this_fd]->flags & O_WRONLY))
            pfds[i].events ^= POLLOUT;

        if (pfds[i].events == 0)
            continue;

        inode_t * ino = current_process->fds[this_fd]->inode;
        if (!(S_ISCHR(ino->mode) || S_ISFIFO(ino->mode))) {
            fds[i].revents = pfds[i].revents = pfds->events;
            ready++;
            continue;
        }
        pfds->ino = ino;
        __atomic_add_fetch(&pfds->ino->instances, 1, __ATOMIC_RELEASE);
        waitable_fds++;
    }
    spinlock_release(&current_process->lock);

    if (waitable_fds == 0)
        goto end;

    unsigned int magic_queue_val = __atomic_load_n(&current_thread->magic_queue_value, __ATOMIC_ACQUIRE);
    while (1) {
        for (nfds_t i = 0; i < nfds; i++) {
            if (!pfds[i].ino)
                continue;

            if (S_ISCHR(pfds[i].ino->mode))
                pfds[i].revents = poll_dev(pfds[i].ino, pfds[i].events);
            else if (S_ISFIFO(pfds[i].ino->mode))
                pfds[i].revents = poll_pipe(pfds[i].ino, pfds[i].events);

            if (pfds[i].revents)
                ready++;
        }

        if (ready) break;

        asm volatile ("cli");
        if (check_eintr()) {
            asm volatile ("sti");
            break;
        }
        if (magic_queue_val == __atomic_load_n(&current_thread->magic_queue_value, __ATOMIC_ACQUIRE)) {
            if (timeout) {
                if (sys_clock_nanosleep(
                    current_process, current_thread,
                    CLOCK_MONOTONIC, 0,
                    ts, &ts) == -EINTR && !check_eintr()) {
                    asm volatile ("sti");
                    continue; // woken up by poll
                }
                asm volatile ("sti");
                // woken up by signal, or timeout expired
                // in either case the correct return is EINTR anyway, so no need to rerun
                break;
            }
            current_thread->status = SCHED_INTERR_SLEEP;
            reschedule();
        }
        asm volatile ("sti");
        if (check_eintr())
            break;
    }

    // invalidate all queues we're part of so we don't have spurious wakeups later
    __atomic_add_fetch(&current_thread->magic_queue_value, 1, __ATOMIC_RELEASE);

    end:
    ready = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = pfds[i].revents;
        if (pfds[i].revents)
            ready++;
        if (pfds[i].ino)
            close_inode(pfds[i].ino);
    }
    kfree(pfds);
    if (check_eintr())
        ready = -EINTR;
    current_thread->sa_mask = oldset;
    return ready;
}