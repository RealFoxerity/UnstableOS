#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <UnstableOS/syscalls.h>

#define __STR_INNER(x) #x
#define STR(x) __STR_INNER(x)

int kill(pid_t pid, int sig) {
    int ret = syscall(SYSCALL_KILL, pid, sig);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int killpg(pid_t pgrp, int sig) {
    if (pgrp < 1) {
        ___set_errno(ESRCH);
        return -1;
    }
    return kill(-pgrp, sig);
}
int tgkill(pid_t tgid, pid_t tid, int sig) {
    int ret = syscall(SYSCALL_TGKILL, tgid, tid, sig);
    if (ret < 0) {
        ___set_errno(-ret);
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
        ___set_errno(EFAULT);
        return -1;
    }
    struct sigaction act2 = *act;
    act2.__restorer = __sigreturn;

    int ret = syscall(SYSCALL_SIGACTION, sig, &act2, oact);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

void (*signal(int sig, void (*func)(int)))(int) {
    if (sigaction(sig, &(struct sigaction) {.sa_handler = func, .sa_flags = SA_RESTART}, NULL) == -1)
        return SIG_ERR;
    return func;
}

int sigprocmask(int how, const sigset_t * __restrict set, sigset_t * __restrict oset) {
    int ret = syscall(SYSCALL_SIGPROCMASK, how, set, oset);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int sigsuspend(const sigset_t *sigmask) {
    int ret = syscall(SYSCALL_SIGSUSPEND, sigmask);
    ___set_errno(-ret);
    return -1; // never returns "successfully"
}

int pthread_sigmask(int how, const sigset_t * __restrict set, sigset_t * __restrict oset) {
    return sigprocmask(how, set, oset);
}

int sigaddset(sigset_t *set, int signo) {
    if (signo < 0 || signo > NSIG_MAX) {
        ___set_errno(EINVAL);
        return -1;
    }
    if (set == NULL) {
        ___set_errno(EFAULT);
        return -1;
    }

    *set |= GET_SIG_MASK(signo);
    return 0;
}

int sigdelset(sigset_t *set, int signo) {
    if (signo < 0 || signo > NSIG_MAX) {
        ___set_errno(EINVAL);
        return -1;
    }
    if (set == NULL) {
        ___set_errno(EFAULT);
        return -1;
    }

    *set &= ~GET_SIG_MASK(signo);
    return 0;
}

int sigemptyset(sigset_t *set) {
    if (set == NULL) {
        ___set_errno(EFAULT);
        return -1;
    }
    *set = (sigset_t)0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (set == NULL) {
        ___set_errno(EFAULT);
        return -1;
    }
    *set = (sigset_t)-1;
    return 0;
}

int sigismember(const sigset_t *set, int signo) {
    if (signo < 0 || signo > NSIG_MAX) {
        ___set_errno(EINVAL);
        return -1;
    }
    if (set == NULL) {
        ___set_errno(EFAULT);
        return -1;
    }

    return (*set & GET_SIG_MASK(signo)) != 0;
}

int sigpending(sigset_t *set) {
    int ret = syscall(SYSCALL_SIGPENDING, set);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return 0;
}

int sighold(int sig) {
    int ret = sigprocmask(SIG_BLOCK, &(sigset_t){GET_SIG_MASK(sig)}, NULL);
    if (ret < 0) {
        ___set_errno(-ret);
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
        ___set_errno(-ret);
        return -1;
    }
    sigaddset(&set, sig);
    return sigsuspend(&set);
}
int sigrelse(int sig) {
    int ret = sigprocmask(SIG_UNBLOCK, &(sigset_t){GET_SIG_MASK(sig)}, NULL);
    if (ret < 0) {
        ___set_errno(-ret);
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

    return tgkill(pid, tid, sig);
}

int sigqueue(pid_t pid, int signo, union sigval value) {
    int ret = syscall(SYSCALL_SIGQUEUE, pid, signo, value);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return 0;
}

#include "signal_msgs.h"
// strictly technically, POSIX says that strsignal shall not be called anywhere internally
// however the buffer is static anyway, and we only do reading, so probably fine
void psignal(int signum, const char * message) {
    if (message != NULL) {
        fprintf(stderr, "%s: ", message);
    }
    fprintf(stderr, "%s\n", strsignal(signum));
}
void psiginfo(const siginfo_t *pinfo, const char *message) {
    if (pinfo == NULL) {
        ___set_errno(EFAULT);
        return;
    }
    psignal(pinfo->si_signo, message);
}

char *strsignal(int signum) {
    if (signum < 0 || signum > sizeof(__signal_msgs)/sizeof(char *)) {
        ___set_errno(EINVAL);
        return NULL;
    }
    if (__signal_msgs[signum] == NULL) {
        return "Unknown signal";
    }
    return (char*)__signal_msgs[signum];
}

#include <pthread.h>
#include <UnstableOS/tls.h>
int pthread_kill(pthread_t thread, int sig) {
    if (thread == NULL) {
        ___set_errno(EFAULT);
        return -1;
    }
    struct process_control_block * pcb = thread->__pcb;
    return tgkill(pcb->pid, thread->__tid, sig);
}

const static char *__sigstrs[] = {
    [SIGHUP]    = "HUP",
    [SIGINT]    = "INT",
    [SIGQUIT]   = "QUIT",
    [SIGILL]    = "ILL",
    [SIGTRAP]   = "TRAP",
    [SIGABRT]   = "ABRT",
    [SIGBUS]    = "BUS",
    [SIGFPE]    = "FPE",
    [SIGKILL]   = "KILL",
    [SIGUSR1]   = "USR1",
    [SIGSEGV]   = "SEGV",
    [SIGUSR2]   = "USR2",
    [SIGPIPE]   = "PIPE",
    [SIGALRM]   = "ALRM",
    [SIGTERM]   = "TERM",
    [SIGSTKFLT] = "STKFLT",
    [SIGCHLD]   = "CHLD",
    [SIGCONT]   = "CONT",
    [SIGSTOP]   = "STOP",
    [SIGTSTP]   = "TSTP",
    [SIGTTIN]   = "TTIN",
    [SIGTTOU]   = "TTOU",
    [SIGURG]    = "URG",
    [SIGXCPU]   = "XCPU",
    [SIGXFSZ]   = "XFSZ",
    [SIGVTALRM] = "VTALRM",
    [SIGPROF]   = "PROF",
    [SIGWINCH]  = "WINCH",
    [SIGIO]     = "IO",
    [SIGPWR]    = "PWR",
    [SIGSYS]    = "SYS",
    [SIGRTMIN]  = "RTMIN",
};

#include <string.h>
int sig2str(int signum, char *str) {
    if (signum <= 0 || signum > SIGRTMAX || !str) {
        ___set_errno(EINVAL);
        return -1;
    }
    if (signum <= SIGRTMIN) {
        strcpy(str,__sigstrs[signum]);
    } else {
        snprintf(str, SIG2STR_MAX, "RTMIN+%d", signum - SIGRTMIN);
    }
    return 0;
}

#include <stdlib.h>

int str2sig(const char *restrict str, int *restrict pnum) {
    if (!str || !pnum) {
        ___set_errno(EFAULT);
        return -1;
    }
    for (int i = 1; i <= SIGRTMIN; i++) {
        if (strcmp(str, __sigstrs[i]) == 0) {
            *pnum = i;
            return 0;
        }
    }
    if (strncmp(str, "RTMIN+", 6) == 0) {
        char * end = NULL;
        int sig = SIGRTMIN + strtoul(str+6, &end, 10);
        if (*end) {
            ___set_errno(EINVAL);
            return -1;
        }
        *pnum = sig;
        return 0;
    }
    if (strncmp(str, "RTMAX-", 6) == 0) {
        char * end = NULL;
        int sig = SIGRTMAX + strtoul(str+6, &end, 10);
        if (*end) {
            ___set_errno(EINVAL);
            return -1;
        }
        *pnum = sig;
        return 0;
    }
    ___set_errno(EINVAL);
    return -1;
}