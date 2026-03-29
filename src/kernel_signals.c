#include "kernel.h"
#include "kernel_interrupts.h"
#include "kernel_sched.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/limits.h>
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"

// TODO: permission checks

static void signal_queue_up(process_t * signaled, const siginfo_t * info) {
    if (info->si_signo == 0) return;

    if (info->si_signo >= SIGRTMIN && info->si_signo <= SIGRTMAX) {
        if (signaled->sa_rt_queue_count > SIGQUEUE_MAX) {
            //kprintf("Full RT signal queue for PID %ld, dropping request\n", signaled->pid);
            return;
        }
        signaled->sa_rt_queue_count ++;
        struct rt_siginfo_ll * ll = kalloc(sizeof(struct rt_siginfo_ll));
        kassert(ll);
        ll->next = NULL;
        ll->info = *info;
        if (signaled->sa_rt_queue == NULL) {
            signaled->sa_rt_queue = ll;
            signaled->sa_rt_queue_last = ll;
        } else {
            signaled->sa_rt_queue_last->next = ll;
            signaled->sa_rt_queue_last = ll;
        }
    } else {
        if (signaled->sa_pending & GET_SIG_MASK(info->si_signo)) {
            //kprintf("Signal number %d was already pending, dropping old request\n", info->si_signo);
        }
        signaled->sa_pending |= GET_SIG_MASK(info->si_signo);
        signaled->sa_pending_info[info->si_signo] = *info;
    }
}

static void signal_queue_remove(process_t * signaled, int sig) {
    if (sig == 0) return;

    if (sig >= SIGRTMIN && sig <= SIGRTMAX) {
        // we don't allow the cancelation of a specific RT signal
        /*
        if (signaled->sa_rt_queue_tail) {
            memmove(signaled->sa_rt_queued,
                    signaled->sa_rt_queued + 1,
                    sizeof(siginfo_t) * signaled->sa_rt_queue_tail);

            signaled->sa_rt_queue_tail--;
        }*/
    } else
        signaled->sa_pending &= ~GET_SIG_MASK(sig);
}

#define SIGNAL_IGN  0
#define SIGNAL_TERM 1
#define SIGNAL_CORE 2
#define SIGNAL_CONT 3
#define SIGNAL_STOP 4
static const char signal_default_actions[NSIG_MAX] = {
    [SIGHUP   ] = SIGNAL_TERM,
    [SIGINT   ] = SIGNAL_TERM,
    [SIGQUIT  ] = SIGNAL_CORE,
    [SIGILL   ] = SIGNAL_CORE,
    [SIGTRAP  ] = SIGNAL_CORE,
    [SIGABRT  ] = SIGNAL_CORE,
    [SIGBUS   ] = SIGNAL_CORE,
    [SIGFPE   ] = SIGNAL_CORE,
    [SIGKILL  ] = SIGNAL_TERM,
    [SIGUSR1  ] = SIGNAL_TERM,
    [SIGSEGV  ] = SIGNAL_CORE,
    [SIGUSR2  ] = SIGNAL_TERM,
    [SIGPIPE  ] = SIGNAL_TERM,
    [SIGALRM  ] = SIGNAL_TERM,
    [SIGTERM  ] = SIGNAL_TERM,
    [SIGSTKFLT] = SIGNAL_TERM,
    [SIGCHLD  ] = SIGNAL_IGN,
    [SIGCONT  ] = SIGNAL_CONT,
    [SIGSTOP  ] = SIGNAL_STOP,
    [SIGTSTP  ] = SIGNAL_STOP,
    [SIGTTIN  ] = SIGNAL_STOP,
    [SIGTTOU  ] = SIGNAL_STOP,
    [SIGURG   ] = SIGNAL_IGN,
    [SIGXCPU  ] = SIGNAL_CORE,
    [SIGXFSZ  ] = SIGNAL_CORE,
    [SIGVTALRM] = SIGNAL_TERM,
    [SIGPROF  ] = SIGNAL_TERM,
    [SIGWINCH ] = SIGNAL_IGN,
    [SIGIO    ] = SIGNAL_TERM,
    [SIGPWR   ] = SIGNAL_TERM,
    [SIGSYS   ] = SIGNAL_CORE,
};

static void signal_parent_cont(process_t * child, siginfo_t * orig_sig) {
    if (child->parent->sa_handlers[SIGCHLD - 1].sa_handler == SIG_IGN ||
        child->parent->sa_handlers[SIGCHLD - 1].sa_flags & SA_NOCLDSTOP)
            return;

    signal_process(child->parent, &(siginfo_t) {
        .si_signo = SIGCHLD,
        .si_code = CLD_CONTINUED,
        .si_pid = child->pid,
        .si_status = orig_sig->si_signo,
        .si_uid = orig_sig->si_uid
    });
}

static void signal_parent_stop(process_t * child, siginfo_t * orig_sig) {
    if (child->parent->sa_handlers[SIGCHLD - 1].sa_handler == SIG_IGN ||
        child->parent->sa_handlers[SIGCHLD - 1].sa_flags & SA_NOCLDSTOP)
            return;

    signal_process(child->parent, &(siginfo_t) {
        .si_signo = SIGCHLD,
        .si_code = CLD_STOPPED,
        .si_pid = child->pid,
        .si_status = orig_sig->si_signo,
        .si_uid = orig_sig->si_uid
    });
}

static void signal_parent_killed(process_t * child, siginfo_t * orig_sig) {
    child->postmortem_wstatus = 0x200 | (orig_sig->si_signo << 12);
    if (child->parent->sa_handlers[SIGCHLD - 1].sa_handler == SIG_IGN)
        return;

    signal_process(child->parent, &(siginfo_t) {
        .si_signo = SIGCHLD,
        .si_code = CLD_KILLED,
        .si_pid = child->pid,
        .si_status = orig_sig->si_signo,
        .si_uid = orig_sig->si_uid
    });
}

static char force_kernel_sigs(process_t * group, thread_t * signaled, siginfo_t * info) {
    switch (info->si_signo) {
        case SIGABRT:
            kprintf("[PID %ld TID %ld SIGABRT]\n", group->pid, signaled->tid);
            group->do_cleanup = 1;
            signal_parent_killed(group, info);
            return 1;
        case SIGBUS:
            kprintf("[PID %ld TID %ld SIGBUS]\n", group->pid, signaled->tid);
            group->do_cleanup = 1;
            signal_parent_killed(group, info);
            return 1;
        case SIGFPE:
            kprintf("[PID %ld TID %ld SIGFPE]\n", group->pid, signaled->tid);
            group->do_cleanup = 1;
            signal_parent_killed(group, info);
            return 1;
        case SIGILL:
            kprintf("[PID %ld TID %ld SIGILL]\n", group->pid, signaled->tid);
            group->do_cleanup = 1;
            signal_parent_killed(group, info);
            return 1;
        case SIGSEGV:
            kprintf("[PID %ld TID %ld SIGSEGV]\n", group->pid, signaled->tid);
            group->do_cleanup = 1;
            signal_parent_killed(group, info);
            return 1;
    }
    return 0;
}

// manage actions for ignored and default actions
// 1 = was handled
static char signal_check_apply_default_action(process_t * group, thread_t * thread, siginfo_t * info) {
    if (info->si_signo == 0) return 1;

    if (group->sa_handlers[info->si_signo - 1].sa_handler == SIG_IGN) {
        if (!(info->si_code & SI_USER)) {
            if (force_kernel_sigs(group, thread, info)) return 1;
        }
        return 1;
    }
    if (group->sa_handlers[info->si_signo - 1].sa_handler == SIG_DFL) {
        switch (signal_default_actions[info->si_signo]) {
            case SIGNAL_IGN:
                return 1;
            case SIGNAL_CONT:
                group->is_stopped = 0;
                signal_parent_cont(group, info);
                signal_queue_remove(group, SIGSTOP);
                signal_queue_remove(group, SIGTSTP);
                return 1;
            case SIGNAL_STOP:
                group->is_stopped = 1;
                signal_parent_stop(group, info);
                signal_queue_remove(group, SIGCONT);
                return 1;
            case SIGNAL_CORE: // TODO: create coredumps :P
            case SIGNAL_TERM:
                signal_parent_killed(group, info);
                group->do_cleanup = 1;
                return 1;
        }
    }
    return 0;
}

static char signal_dispatch_thread(process_t * group, thread_t * signaled, siginfo_t * info, char queue_up) {
    if (info->si_signo == 0) return 1;

    if (group->pid == 1) {
        switch (info->si_signo) {
            case SIGKILL:
                kprintf("Tried to SIGKILL init, ignoring request\n");
                return 1;
            case SIGSTOP:
            case SIGTSTP:
                kprintf("Tried to SIGSTOP/SIGTSTP init, ignoring request\n");
                return 1;
        }
    }
    // cannot be ignored, blocked, changed; might as well do them now
    switch (info->si_signo) {
        case SIGKILL:
            group->do_cleanup = 1;
            signal_parent_killed(group, info);
            return 1;
        case SIGCONT: // not 100% sure about this one (linux works like this though)
            group->is_stopped = 0;
            signal_parent_cont(group, info);
            signal_queue_remove(group, SIGSTOP);
            signal_queue_remove(group, SIGTSTP);
            return 1;
        case SIGSTOP:
            group->is_stopped = 1;
            signal_parent_stop(group, info);
            signal_queue_remove(group, SIGCONT);
            return 1;
    }


    // SIGKILL, SIGCONT, and SIGSTOP allow kernel threads to continue
    // so we don't have to check UNINTERR_SLEEP for them
    if (signaled->status == SCHED_UNINTERR_SLEEP) {
        if (queue_up) signal_queue_up(group, info);
        return 0;
    }

    if ((signaled->sa_mask & GET_SIG_MASK(info->si_signo) ||
            group->sa_handlers[info->si_signo - 1].sa_handler == SIG_IGN) &&
        !(info->si_code & SI_USER)) {
        // signals raised by hardware/kernel cannot be ignored/masked
        if (force_kernel_sigs(group, signaled, info)) return 1;
        /*
        kprintf("Warning: Unexpected kernel raised signal to PID %ld TID %ld - %d\n", group->pid, signaled->tid, info->si_signo);
        return 1;
        */
    } else if (signaled->sa_mask & GET_SIG_MASK(info->si_signo)) {
        if (queue_up) signal_queue_up(group, info);
        return 0;
    }

    if (signaled->sa_to_be_handled) {
        if (queue_up) signal_queue_up(group, info);
        return 0;
    }

    if (info->si_signo < SIGRTMIN) signal_queue_remove(group, info->si_signo);
    signaled->sa_to_be_handled = info->si_signo;
    signaled->sa_info_to_be_handled = *info;
    signaled->status = SCHED_RUNNABLE;
    return 1;
}

static void signal_dispatch_process(process_t * signaled, siginfo_t * sig) {
    for (thread_t * thread = signaled->threads; thread != NULL; thread = thread->next) {
        if (thread->sa_to_be_handled) continue; // already planned signal
        if (signal_dispatch_thread(signaled, thread, sig, 0))
            return;
    }
    signal_queue_up(signaled, sig);
}

// lock scheduler beforehand or have interrupts disabled
void signal_process(process_t * signaled, siginfo_t * sig) {
    if (sig->si_signo <  0 || sig->si_signo > NSIG_MAX) return;
    if (signaled->pid == 0 || signaled->ring == 0) return; // we don't want to signal the kernel

    //kprintf("signaling pid %ld, with %d\n", signaled->pid, sig->si_signo);
    signal_dispatch_process(signaled, sig);
}

// retries all pending signals
void signal_retry_process(process_t * signaled) {
    for (int i = 0; i < SIGRTMIN - 1; i++) {
        if (!(signaled->sa_pending & GET_SIG_MASK(i+1))) continue;
        for (thread_t * thread = signaled->threads; thread != NULL; thread = thread->next) {
            if (thread->sa_to_be_handled) continue; // already planned signal
            if (signal_dispatch_thread(signaled, thread, &signaled->sa_pending_info[i], 0)) {
                break;
            }
        }
    }

    for (struct rt_siginfo_ll * curr = signaled->sa_rt_queue, *prev = NULL; curr != NULL; ) {
        char found = 0;
        for (thread_t * thread = signaled->threads; thread != NULL; thread = thread->next) {
            if (thread->sa_to_be_handled) continue;
            if (signal_dispatch_thread(signaled, thread, &curr->info, 0)) {
                found = 1;
                break;
            }
        }
        if (found) {
            signaled->sa_rt_queue_count --;
            if (prev == NULL) {
                signaled->sa_rt_queue = curr;
            } else {
                prev->next = curr->next;
                if (curr->next == NULL) {
                    signaled->sa_rt_queue_last = prev;
                }
            }
            struct rt_siginfo_ll * next = curr->next;
            kfree(curr);
            curr = next;
            continue;
        }
        prev = curr;
        curr = curr->next;
    }
}

void signal_thread(process_t * group, thread_t * thread, siginfo_t * sig) {
    if (sig->si_signo < 0 || sig->si_signo > NSIG_MAX) return;
    if (group->pid == 0   || group->ring == 0) return; // we don't want to signal the kernel

    //kprintf("signaling pid %ld, tid %ld, with %d\n", group->pid, thread->tid, sig->si_signo);
    signal_dispatch_thread(group, thread, sig, 1);
}

static long signal_send_thread(pid_t tgid, pid_t tid, siginfo_t * sig) {
    for (process_t * signaled = process_list; signaled != NULL; signaled = signaled->next) {
        if (signaled->pid == tgid) {
            for (thread_t * thread = signaled->threads; thread != NULL; thread = thread->next) {
                if (thread->tid == tid) {
                    signal_thread(signaled, thread, sig);
                    return 0;
                }
            }
            return -ESRCH;
        }
    }
    return -ESRCH;
}

static long signal_send_process(pid_t pid, siginfo_t * sig) {
    for (process_t * signaled = process_list; signaled != NULL; signaled = signaled->next) {
        if (signaled->pid == pid) {
            signal_process(signaled, sig);
            return 0;
        }
    }
    return -ESRCH;
}

static long signal_send_process_group(pid_t pgrp, siginfo_t * sig) {
    char found = 0;
    for (process_t * signaled = process_list; signaled != NULL; signaled = signaled->next) {
        if (signaled->pgrp == pgrp) {
            signal_process(signaled, sig);
            found = 1;
        }
    }
    if (found) return 0;
    return -ESRCH;
}

static void signal_send_every_process(siginfo_t * sig) {
    for (process_t * signaled = process_list; signaled != NULL; signaled = signaled->next) {
        signal_process(signaled, sig);
    }
}

int sys_kill(pid_t pid, int sig) {
    if (sig < 0 || sig > NSIG_MAX) return -EINVAL;
    long ret = 0;

    siginfo_t info = {
        .si_signo = sig,
        .si_code = SI_USER,
    };

    spinlock_acquire(&scheduler_lock);
    if (pid > 0) { // normal kill behavior
        ret = signal_send_process(pid, &info);
    } else if (pid == 0) { // process group
        ret = signal_send_process_group(current_process->pgrp, &info);
    } else if (pid == -1) {
        signal_send_every_process(&info);
    } else {
        ret = signal_send_process_group(-pid, &info);
    }
    spinlock_release(&scheduler_lock);

    if (ret == 0) reschedule(); // deliver the signals
    return ret;
}

int sys_tgkill(pid_t tgid, pid_t tid, int sig) {
    if (tgid < 0 || tid < 0) return -EINVAL;
    if (sig < 0 || sig > NSIG_MAX) return -EINVAL;

    siginfo_t info = {
        .si_signo = sig,
        .si_code = SI_USER,
        .si_uid = current_process->uid // not exactly correct, but useful
    };

    long ret = 0;
    spinlock_acquire(&scheduler_lock);

    ret = signal_send_thread(tgid, tid, &info);

    spinlock_release(&scheduler_lock);
    if (ret == 0) reschedule();
    return ret;
}

// we need this, because normal kill() can't handle pgrp 0 and 1
int signal_process_group(pid_t process_group, siginfo_t * info) {
    if (process_group < 0) return -EINVAL;
    if (info->si_signo < 0 || info->si_signo >= NSIG_MAX) return -EINVAL;

    spinlock_acquire(&scheduler_lock);

    signal_send_process_group(process_group, info);

    spinlock_release(&scheduler_lock);

    reschedule();
    return 0;
}

struct {
    void * eip;
    void * ebp;
    siginfo_t info;


    siginfo_t * info_ptr;
    int sig;
} typedef sa_stack_state;


#define SAFE_SA_MASK (~(\
    GET_SIG_MASK(SIGKILL) |\
    GET_SIG_MASK(SIGSTOP) |\
    GET_SIG_MASK(SIGCONT)))


static char check_address_writable(const void * addr, size_t n) {
    if (addr == NULL) return 0;
    n += (unsigned long)addr & (PAGE_SIZE_NO_PAE - 1);
    addr = (void*)((unsigned long) addr & ~(PAGE_SIZE_NO_PAE - 1));

    for (const void * iteraddr = addr; iteraddr < addr+n && iteraddr >= addr; iteraddr += PAGE_SIZE_NO_PAE) {
        PAGE_TABLE_TYPE * pte = paging_get_pte(iteraddr);
        if (pte == NULL) return 0;
        if (!(*pte & PTE_PDE_PAGE_USER_ACCESS)) return 0;
        if (!(*pte & PTE_PDE_PAGE_WRITABLE)) return 0;
    }
    return 1;
}

// due to the way the System V ABI for IA-32 works, we can push arguments as if for sigaction
struct signal_stack_state {
    void * restorer_eip; // so that calling ret from userspace actually works and gets us to the restorer
    int sig;
    siginfo_t  * info;
    ucontext_t * ctx;

    sigset_t     previous_sa_mask;
    ucontext_t __ctx;
    siginfo_t  __info;
};

void signal_dispatch_sa(process_t * group, thread_t * thread) {
    if (thread->sa_to_be_handled == 0) return;

    // default actions don't need to hijack the thread;
    if (signal_check_apply_default_action(group, thread, &thread->sa_info_to_be_handled)) {
        thread->sa_to_be_handled = 0;
        return;
    }

    if ((thread->context.iret_frame.cs & 3) == 0) return;

    if (!(
        thread->context.iret_frame.sp <  PROGRAM_STACK_VADDR &&
        thread->context.iret_frame.sp >= PROGRAM_STACK_VADDR - PROGRAM_STACK_SIZE * PTHREAD_THREADS_MAX) ||
        !check_address_writable(thread->context.iret_frame.sp - sizeof(struct signal_stack_state), sizeof(struct signal_stack_state))
    ) {
        kprintf("Warning: PID %ld TID %ld invalid ESP (%p) on signal dispatch - segmentation fault, terminating!\n", group->pid, thread->tid, thread->context.iret_frame.sp);
        thread->sa_to_be_handled = 0; // to be sure
        group->do_cleanup = 1;
        return;
    }

    struct signal_stack_state * sss = thread->context.iret_frame.sp - sizeof(struct signal_stack_state);

    sss->restorer_eip     = current_process->sa_handlers[thread->sa_to_be_handled - 1].__restorer;
    sss->previous_sa_mask = thread->sa_mask;

    if (!(group->sa_handlers[thread->sa_to_be_handled - 1].sa_flags & SA_NODEFER)) {
        thread->sa_mask |= GET_SIG_MASK(thread->sa_to_be_handled);
    }
    thread->sa_mask |= group->sa_handlers[thread->sa_to_be_handled - 1].sa_mask;
    thread->sa_mask &= SAFE_SA_MASK;

    sss->__ctx = (ucontext_t) {
        .uc_link      = NULL,
        .uc_sigmask   = thread->sa_mask,
        .uc_stack     = (stack_t) {
            .ss_flags = SS_DISABLE
        },
        .uc_mcontext  = thread->context,
    };
    sss->__info = thread->sa_info_to_be_handled;

    sss->ctx    = &sss->__ctx;
    sss->info   = &sss->__info;
    sss->sig    = thread->sa_to_be_handled;

    thread->context.iret_frame.sp -= sizeof(struct signal_stack_state);
    thread->context.iret_frame.ip  =
        group->sa_handlers[thread->sa_to_be_handled - 1].sa_handler;

    if (group->sa_handlers[thread->sa_to_be_handled - 1].sa_flags & SA_RESETHAND)
        group->sa_handlers[thread->sa_to_be_handled - 1] = (struct sigaction){0};

    thread->sa_to_be_handled = 0;
}

#include "include/lowlevel.h"
#define SAFE_EFL_MASK (       \
    IA_32_EFL_STATUS_CARRY  | \
    IA_32_EFL_STATUS_PARITY | \
    IA_32_EFL_STATUS_ADJUST | \
    IA_32_EFL_STATUS_ZERO   | \
    IA_32_EFL_STATUS_SIGN     \
)
static void sigreturn_restore_context(mcontext_t * target_ctx, mcontext_t * source_ctx) {
    memcpy(target_ctx, source_ctx, sizeof(mcontext_t) - sizeof(struct interr_frame));
    target_ctx->iret_frame.ip     = source_ctx->iret_frame.ip;
    target_ctx->iret_frame.sp     = source_ctx->iret_frame.sp;
    target_ctx->iret_frame.flags &= ~SAFE_EFL_MASK; // remove all status flags
    target_ctx->iret_frame.flags |= source_ctx->iret_frame.flags & SAFE_EFL_MASK;
}

void sys_sigreturn(mcontext_t * ctx) {
    if (!(
        ctx->iret_frame.sp < PROGRAM_STACK_VADDR &&
        ctx->iret_frame.sp >= PROGRAM_STACK_VADDR - PROGRAM_STACK_SIZE * PTHREAD_THREADS_MAX) ||
        !check_address_writable(ctx->iret_frame.sp, sizeof(struct signal_stack_state))
    ) {
        kprintf("Warning: PID %ld TID %ld invalid ESP on signal return - segmentation fault, terminating!\n", current_process->pid, current_thread->tid);
        current_process->do_cleanup = 1;
        return;
    }

    struct signal_stack_state * sss = ctx->iret_frame.sp - sizeof(void*); // the restorer_eip will be popped off during ret

    current_thread->sa_mask  = sss->previous_sa_mask;
    current_thread->sa_mask &= SAFE_SA_MASK;

    sigreturn_restore_context(ctx, &sss->__ctx.uc_mcontext);
}

int sys_sigaction(int sig, struct sigaction * __restrict act, struct sigaction * __restrict oact) {
    if (sig < 0 || sig > NSIG_MAX) return -EINVAL;
    switch (sig) {
        case SIGKILL:
        case SIGSTOP:
        case SIGCONT:
            return -EINVAL;
    }
    if (oact != NULL)
        memcpy(oact, &current_process->sa_handlers[sig-1], sizeof(struct sigaction));

    if (act->sa_handler == SIG_HOLD) {
        current_thread->sa_mask |= GET_SIG_MASK(sig);
        current_thread->sa_mask &= SAFE_SA_MASK;
        return 0;
    }

    memcpy(&current_process->sa_handlers[sig-1], act, sizeof(struct sigaction));
    current_process->sa_handlers[sig-1].sa_flags &= SAFE_SA_MASK;
    return 0;
}

int sys_sigprocmask(int how, const sigset_t * __restrict set, sigset_t * oset) {
    switch (how) {
        default:
            return -EINVAL;
        case SIG_BLOCK:
        case SIG_SETMASK:
        case SIG_UNBLOCK:
    }

    if (oset != NULL)
        *oset = current_thread->sa_mask;
    if (set == NULL) return 0;

    switch (how) {
        case SIG_BLOCK:
            current_thread->sa_mask |= *set;
            break;
        case SIG_SETMASK:
            current_thread->sa_mask = *set;
            break;
        case SIG_UNBLOCK:
            current_thread->sa_mask &= ~*set;
    }
    current_thread->sa_mask &= SAFE_SA_MASK;
    return 0;
}

int sys_sigsuspend(const sigset_t * set) {
    if (current_thread->sa_to_be_handled) return -EINTR;

    sigset_t old_set = current_thread->sa_mask;
    sigset_t new_set = *set;
    new_set &= SAFE_SA_MASK;

    current_thread->sa_mask = new_set;

    asm volatile ("sti;");

    while (!current_thread->sa_to_be_handled) reschedule();

    current_thread->sa_mask = old_set;
    return -EINTR;
}

int sys_sigqueue(pid_t pid, int signo, union sigval value) {
    if (signo < 0 || signo > NSIG_MAX) return -EINVAL;
    long ret = 0;

    siginfo_t info = {
        .si_signo = signo,
        .si_code = SI_USER,
        .si_value = value
    };

    spinlock_acquire(&scheduler_lock);

    ret = signal_send_process(pid, &info);

    spinlock_release(&scheduler_lock);

    return ret;
}