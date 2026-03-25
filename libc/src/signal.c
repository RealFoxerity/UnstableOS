#include "include/signal.h"
#include "include/unistd.h"
#include "../../src/include/errno.h"
#include "../../src/include/kernel.h"


int kill(pid_t pid, int sig) {
    int ret = syscall(SYSCALL_KILL, pid, sig);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
int killpg(pid_t pgrp, int sig) {
    if (pgrp < 1) {
        errno = ESRCH;
        return -1;
    }
    return kill(-pgrp, sig);
}
int tgkill(pid_t tgid, pid_t tid, int sig) {
    int ret = syscall(SYSCALL_TGKILL, tgid, tid, sig);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

__attribute__((naked, noreturn)) static void __sigreturn() {
    asm volatile (
        "mov %0, %%eax;"
        "int $"STR(SYSCALL_INTERR)
        ::"i"(SYSCALL_SIGRETURN)
    );
}

int sigaction(int sig, const struct sigaction *__restrict act, struct sigaction *__restrict oact) {
    if (act == NULL) {
        errno = EFAULT;
        return -1;
    }
    struct sigaction act2 = *act;
    act2.__restorer = __sigreturn;

    int ret = syscall(SYSCALL_SIGACTION, sig, &act2, oact);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

void (*signal(int sig, void (*func)(int)))(int) {
    if (sigaction(sig, &(struct sigaction) {.sa_handler = func}, NULL) == -1)
        return SIG_ERR;
    return func;
}

int sigprocmask(int how, const sigset_t * __restrict set, sigset_t * __restrict oset) {
    int ret = syscall(SYSCALL_SIGPROCMASK, how, set, oset);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
int sigsuspend(const sigset_t *sigmask) {
    int ret = syscall(SYSCALL_SIGSUSPEND, sigmask);
    errno = -ret;
    return -1; // never returns "successfully"
}

int pthread_sigmask(int how, const sigset_t * __restrict set, sigset_t * __restrict oset) {
    return sigprocmask(how, set, oset);
}

int sigaddset(sigset_t *set, int signo) {
    if (signo < 0 || signo > NSIG_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (set == NULL) {
        errno = EFAULT;
        return -1;
    }

    *set &= ~GET_SIG_MASK(signo);
    return 0;
}

int sigdelset(sigset_t *set, int signo) {
    if (signo < 0 || signo > NSIG_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (set == NULL) {
        errno = EFAULT;
        return -1;
    }

    *set |= GET_SIG_MASK(signo);
    return 0;
}

int sigemptyset(sigset_t *set) {
    if (set == NULL) {
        errno = EFAULT;
        return -1;
    }
    *set = (sigset_t)-1;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (set == NULL) {
        errno = EFAULT;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigismember(const sigset_t *set, int signo) {
    if (signo < 0 || signo > NSIG_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (set == NULL) {
        errno = EFAULT;
        return -1;
    }

    return (*set & GET_SIG_MASK(signo)) == 0;
}

int sigpending(sigset_t *set) {
    int ret = syscall(SYSCALL_SIGPENDING, set);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int sighold(int sig) {
    int ret = sigprocmask(SIG_BLOCK, &(sigset_t){GET_SIG_MASK(sig)}, NULL);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}
int sigignore(int sig) {
    if (signal(sig, SIG_IGN) == SIG_ERR)
        return -1; // errno set by signal()
    return 0;
}
int sigpause(int sig) {
    sigset_t set = 0;
    int ret = sigprocmask(SIG_SETMASK, NULL, &set);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    sigaddset(&set, sig);
    return sigsuspend(&set);
}
int sigrelse(int sig) {
    int ret = sigprocmask(SIG_UNBLOCK, &(sigset_t){GET_SIG_MASK(sig)}, NULL);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}
void (*sigset(int sig, void (*disp)(int)))(int) {
    return signal(sig, disp);
}


int raise(int sig) {
    pid_t pid, tid;
    pid = getpid();
    tid = gettid();

    int ret = tgkill(pid, tid, sig);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int sigqueue(pid_t pid, int signo, union sigval value) {
    int ret = syscall(SYSCALL_SIGQUEUE, pid, signo, value);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}