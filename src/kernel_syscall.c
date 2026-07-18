#include <stdint.h>

#include "kernel_exec.h"
#include "kernel_interrupts.h"
#include "kernel.h"
#include <string.h>
#include <time.h>
#include <sys/times.h>
#include "mm/kernel_memory.h"
#include <errno.h>
#include "kernel_sched.h"
#include "kernel_semaphore.h"
#include "fs/fs.h"
#include <pthread.h>
#include "kernel_gdt_idt.h"

#define kprintf(fmt, ...) kprintf("Kernel Routines: "fmt, ##__VA_ARGS__)

extern void clear_screen_fatal(); // kernel_interrupts.c

#define SYSCALL_PANIC_TEXT " #### RING 2 INDUCED PANIC; HALTING #### "


void kernel_syscall_dispatcher(mcontext_t * ctx);
// since we use system V abi, arg4 is pushed onto the stack by the user
__attribute__((naked, no_caller_saved_registers)) void interr_syscall(struct interr_frame * interrupt_frame) {
    asm volatile (
        "cld;"
        "call fix_segments;"
        "pusha;"
        "pushl %esp;"
        "call kernel_syscall_dispatcher;"
        "popl %esp;"
        "cli;"
        "call fix_segments;"
        "popa;"
        "iret;"
    );
}

void kernel_syscall_dispatcher(mcontext_t * ctx) {
    kassert(current_process);
    kassert(current_thread);

    // we might want to call syscalls from other syscalls and/or drivers
    char in_kernel = (ctx->iret_frame.cs & 3) == 0;

    // check whether the userspace stack is still valid
    if (!paging_check_address_range(ctx->iret_frame.sp - 16, 32, 1, in_kernel)) {
        kprintf("Thread %lu of process %lu entered syscall with invalid stack, segv\n",
            current_thread->tid, current_process->pid);
        current_process->do_cleanup = 1;
        reschedule();
        kernel_idle();
    }

    enum syscalls syscall_number = ctx->eax;
    long
    arg1 = ctx->edi,
    arg2 = ctx->esi,
    arg3 = ctx->edx,
    arg4 = ((long*)(ctx->iret_frame.sp))[0],
    arg5 = ((long*)(ctx->iret_frame.sp))[1];

    long return_value = -ENOSYS;

    #ifndef EXIT_AFFECTS_SYSCALLS
    CRIT_SEC_START
    #endif
    siginfo_t exited_child_status;
    switch (syscall_number) {
        case SYSCALL_YIELD:
            reschedule();
            return_value = 0;
            break;
        case SYSCALL_CREATE_THREAD:
            spinlock_acquire(&scheduler_lock);
            // theoretically don't have to check bounds since they would just cause a segmentation fault
            thread_t * new = kernel_create_thread(current_process, current_thread, (void*)arg1, (void*)arg2, arg3);
            if (!new)
                return_value = 0;
            else
                return_value = (long)new->tcb;
            spinlock_release(&scheduler_lock);
            break;
        case SYSCALL_EXIT_THREAD:
            #ifndef EXIT_AFFECTS_SYSCALLS
            CRIT_SEC_END
            #endif
            current_thread->status = SCHED_THREAD_CLEANUP;
            reschedule();
            kernel_idle();

        case SYSCALL_EXIT:
        case SYSCALL_ABORT:
            if (syscall_number == SYSCALL_ABORT) {
                // so that we can keep the fall-through for syscall_exit
                kprintf("Thread %lu of process %lu called abort()!\n", current_thread->tid, current_process->pid);
                // idk, but seems reasonable
                current_process->postmortem_wstatus = 0x100 | (SIGABRT << 12);
                exited_child_status = (siginfo_t){
                    .si_signo  = SIGCHLD,
                    .si_code   = CLD_KILLED,
                    .si_pid    = current_process->pid,
                    .si_status = SIGABRT
                };
            } else {
                current_process->postmortem_wstatus = arg1 & 0xFF;
                exited_child_status = (siginfo_t){
                    .si_signo  = SIGCHLD,
                    .si_code   = CLD_EXITED,
                    .si_pid    = current_process->pid,
                    .si_status = arg1
                };
            }
            current_process->pending_sigchld_info = exited_child_status;
            current_process->pending_waiting      = 1;

            signal_process(current_process->parent, &exited_child_status);

            current_process->do_cleanup = 1;
            current_thread->in_critical_section = 0;
            #ifndef EXIT_AFFECTS_SYSCALLS
            CRIT_SEC_END
            #endif
            reschedule();
            kernel_idle();

        case SYSCALL_BRK:
            if (current_process->ring == 0) panic("Called brk in a kernel task!");
            if ((void *)arg1 < PROGRAM_HEAP_VADDR ||
                (void *)arg1 >= PROGRAM_HEAP_VADDR + PROGRAM_MAX_HEAP_SIZE)
            {
                return_value = (unsigned long)current_process->program_break;
                break;
            }

            if ((void *)arg1 > current_process->program_break) {
                current_process->program_break = (void *)arg1;
                return_value = arg1;
                break;
            }
            void * old_break = current_process->program_break;
            current_process->program_break = (void *)arg1;
            paging_unmap(current_process->program_break,
                old_break - current_process->program_break);
            return_value = arg1;
            break;

        case SYSCALL_READ:
            if (!paging_check_address_range((void*)arg2, (size_t)arg3, 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti;");
            return_value = sys_read(arg1, (void*)arg2, (size_t)arg3);
            break;
        case SYSCALL_WRITE:
            if (!paging_check_address_range((const void*)arg2, (size_t)arg3, 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti;");
            return_value =  sys_write(arg1, (const void*)arg2, (size_t)arg3);
            break;
        case SYSCALL_PREAD:
            if (!paging_check_address_range((void*)arg2, (size_t)arg3, 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti;");
            return_value = sys_pread(arg1, (void*)arg2, (size_t)arg3, arg4);
            break;
        case SYSCALL_PWRITE:
            if (!paging_check_address_range((const void*)arg2, (size_t)arg3, 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti;");
            return_value = sys_pwrite(arg1, (const void*)arg2, arg3, arg4);
            break;
        case SYSCALL_TRUNC:
            if (!paging_check_address_range((const void*)arg2, sizeof(off_t), 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile("sti;");
            return_value = sys_trunc(arg1, *(off_t*)arg2);
            break;
        case SYSCALL_FCNTL:
            asm volatile("sti;");
            return_value = sys_fcntl(arg1, arg2, arg3);
            break;
        case SYSCALL_SYNC:
            asm volatile("sti;");
            return_value = 0;
            extern void hd_cache_flush();
            hd_cache_flush();
            break;
        case SYSCALL_PIPE2:
            if (!paging_check_address_range((int *)arg1, 2*sizeof(int), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            return_value = sys_pipe((int *)arg1, arg2);
            break;
        case SYSCALL_DUP:
            asm volatile ("sti;");
            return_value = sys_dup(arg1);
            break;
        case SYSCALL_DUP3:
            asm volatile ("sti;");
            return_value = sys_dup3(arg1, arg2, arg3);
            break;
        case SYSCALL_RENAMEAT:
            asm volatile ("sti;");
            return_value = sys_renameat(arg1, (const char *)arg2, arg3, (const char *)arg4);
            break;
        case SYSCALL_UNLINKAT:
            asm volatile ("sti;");
            return_value = sys_unlinkat(arg1, (const char *)arg2, arg3);
            break;
        case SYSCALL_SEEK:
            if (!paging_check_address_range((off_t*)arg2, sizeof(off_t), 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            if (!paging_check_address_range((off_t*)arg4, sizeof(off_t), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti;");

            off_t out = sys_seek(arg1, *(off_t*)arg2, arg3);
            if (out >= 0) {
                *(off_t*)arg4 = out;
                return_value = 0;
            } else {
                return_value = (long)out; // negative errors
            }
            break;
        case SYSCALL_READDIR:
            if (!paging_check_address_range((void*)arg2, arg3, 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti;");
            return_value = sys_readdir(arg1, (void*)arg2, arg3);
            break;
        case SYSCALL_SEM_INIT:
            for (int i = 0; i < SEM_NSEMS_MAX; i++) {
                if (current_process->semaphores[i] == NULL) {
                    current_process->semaphores[i] = kalloc(sizeof(sem_t));
                    memset(current_process->semaphores[i], 0, sizeof(sem_t));
                }
                if (!current_process->semaphores[i]->used) {
                    memset(&current_process->semaphores[i]->waiting_queue, 0, sizeof(thread_queue_t));
                    current_process->semaphores[i]->used = 1;
                    current_process->semaphores[i]->value = arg1;
                    return_value = i;
                    goto syscall_exit;
                }
            }
            return_value = -ENOLCK;
            break;
        case SYSCALL_FUTEX:
            // address checked in the function
            return_value = sys_futex((uint32_t *)arg1, arg2, arg3, arg4, (struct timespec *)arg5);
            break;
        case SYSCALL_SEM_POST:
            if (arg1 < 0 || arg1 >= SEM_NSEMS_MAX) {
                return_value = -EINVAL;
                break;
            }
            if (current_process->semaphores[arg1] == NULL ||
                !current_process->semaphores[arg1]->used
            ) {
                return_value = -EINVAL;
                break;
            }
            if (current_process->semaphores[arg1]->value + 1 == 0) { // TODO: fix for atomicity?
                return_value = -ERANGE;
                break;
            }
            asm volatile ("sti;");

            kernel_sem_post(current_process, arg1);
            break;
        case SYSCALL_SEM_WAIT:
            if (arg1 < 0 || arg1 >= SEM_NSEMS_MAX) {
                return_value = -EINVAL;
                break;
            }
            if (current_process->semaphores[arg1] == NULL ||
                !current_process->semaphores[arg1]->used
            ) {
                kprintf("Thread %lu of process %lu called sem_wait on invalid semaphore (%lu)\n", current_thread->tid, current_process->pid, arg1);

                return_value = -EINVAL;
                break;
            }
            asm volatile ("sti;");

            kernel_sem_wait(current_process, current_thread, arg1);
            break;
        case SYSCALL_SEM_DESTROY:
            if (arg1 < 0 || arg1 >= SEM_NSEMS_MAX) {
                return_value = -EINVAL;
                break;
            }
            if (current_process->semaphores[arg1] == NULL ||
                !current_process->semaphores[arg1]->used) {
                return_value = -EINVAL;
                break;
            }

            // TODO: not thread safe, fix
            if (__atomic_sub_fetch(&current_process->semaphores[arg1]->used, 1, __ATOMIC_RELEASE) == 0)
                kfree(current_process->semaphores[arg1]);
            current_process->semaphores[arg1] = NULL;
            break;
        case SYSCALL_GETPID:
            return_value = current_process->pid;
            break;
        case SYSCALL_GETPPID:
            return_value = current_process->parent->pid;
            break;
        case SYSCALL_GETTID:
            return_value = current_thread->tid;
            break;
        case SYSCALL_GETSID:
            return_value = sys_getsid(arg1);
            break;
        case SYSCALL_SETSID:
            return_value = sys_setsid();
            break;
        case SYSCALL_GETPGID:
            return_value = sys_getpgid(arg1);
            break;
        case SYSCALL_SETPGID:
            return_value = sys_setpgid(arg1, arg2);
            break;
        case SYSCALL_MOUNT:
            asm volatile ("sti");
            return_value = sys_mount((const char*)arg1, (const char*)arg2, (unsigned char)arg3, (unsigned short)arg4);
            break;
        case SYSCALL_UMOUNT:
            asm volatile ("sti");
            return_value = sys_umount((const char*)arg1);
            break;
        case SYSCALL_OPENAT:
            asm volatile ("sti;");
            return_value = sys_openat(arg1, (const char *)arg2, arg3, arg4);
            break;
        case SYSCALL_UMASK:
            arg1 &= 0777;
            mode_t old_umask = current_process->umask;
            current_process->umask = arg1;
            // even though mode_t is an uint, this should never wrap as we don't use that many bits
            return_value = (long)old_umask;
            break;
        case SYSCALL_CLOSE:
            asm volatile ("sti;");
            return_value = sys_close(arg1);
            break;
        case SYSCALL_CHDIR:
            asm volatile ("sti;");
            return_value = sys_chdir((const char *)arg1);
            break;
        case SYSCALL_CHROOT:
            asm volatile ("sti;");
            return_value = sys_chroot((const char *)arg1);
            break;
        case SYSCALL_EXEC:
            asm volatile ("sti;");
            return_value = sys_execve((const char *)arg1, (char * const*)arg2, (char * const*)arg3);
            break;
        case SYSCALL_SPAWN:
            asm volatile ("sti;");
            return_value = sys_spawn((const char *)arg1, (char * const*)arg2, (char * const*)arg3);
            break;
        case SYSCALL_FORK:
            return_value = sys_fork(ctx);
            break;
        case SYSCALL_WAITPID:
            if ((int*)arg2 != NULL) {
                if (!paging_check_address_range((int*)arg2, sizeof(int), 1, in_kernel)) {
                    return_value = -EFAULT;
                    break;
                }
            }
            return_value = sys_waitpid(arg1, (int*)arg2, arg3);
            break;
        case SYSCALL_WAITID:
            if (!paging_check_address_range((siginfo_t*)arg3, sizeof(siginfo_t), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            return_value = sys_waitid(arg1, arg2, (siginfo_t*)arg3, arg4);
            break;
        case SYSCALL_FSTAT:
            if (!paging_check_address_range((void*)arg2, sizeof(struct stat), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti");
            return_value = sys_fstat(arg1, (struct stat *)arg2);
            break;

        case SYSCALL_FSTATAT:
            if (!paging_check_address_range((void*)arg2, sizeof(struct stat), 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti");
            return_value = sys_fstatat(arg1, (const char*) arg2, (struct stat *)arg3, arg4);
            break;
        case SYSCALL_NANOSLEEP:
            if (!paging_check_address_range((void*)arg1, sizeof(struct timespec), 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            if ((struct timespec *)arg2 != NULL && !paging_check_address_range((void*)arg1, sizeof(struct timespec), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            asm volatile ("sti");
            return_value = sys_nanosleep(current_process, current_thread, *(struct timespec*)arg1, (struct timespec*)arg2);
            break;
        case SYSCALL_ALARM:
            return_value = (long)sys_alarm((unsigned)arg1);
            break;
        case SYSCALL_TIME:
            if (!paging_check_address_range((void*)arg1, sizeof(time_t), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            *(time_t*)arg1 = system_time_sec;
            break;
        case SYSCALL_TIMES:
            if (!paging_check_address_range((void*)arg1, sizeof(struct tms), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            if (!paging_check_address_range((void*)arg2, sizeof(clock_t), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            *(struct tms*)arg1 = (struct tms) {
                .tms_utime = current_process->user_clicks,
                .tms_stime = current_process->system_clicks,
                .tms_cutime = current_process->dead_user_clicks,
                .tms_cstime = current_process->dead_system_clicks
            };
            *(clock_t *)arg2 = uptime_clicks;
            break;
        case SYSCALL_KILL:
            return_value = sys_kill(arg1, (int)arg2);
            break;
        case SYSCALL_TGKILL:
            return_value = sys_tgkill(arg1, arg2, arg3);
            break;
        case SYSCALL_SIGACTION:
            if (!paging_check_address_range((void*)arg2, sizeof(struct sigaction), 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            if ((struct sigaction *)arg3 != NULL && !paging_check_address_range((void*)arg3, sizeof(struct sigaction), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            return_value = sys_sigaction(arg1, (struct sigaction *)arg2, (struct sigaction *)arg3);
            break;
        case SYSCALL_SIGRETURN:
            sys_sigreturn(ctx);
            break;
        case SYSCALL_SIGPROCMASK:
            if (!paging_check_address_range((void*)arg2, sizeof(sigset_t), 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            if ((struct sigaction *)arg3 != NULL && !paging_check_address_range((void*)arg3, sizeof(sigset_t), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            return_value = sys_sigprocmask(arg1, (const sigset_t *)arg2, (sigset_t *)arg3);
            break;
        case SYSCALL_SIGPENDING:
            if (!paging_check_address_range((void*)arg1, sizeof(sigset_t), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            *(sigset_t *)arg1 = current_process->sa_pending;
            return_value = 0;
            break;
        case SYSCALL_SIGSUSPEND:
            if (!paging_check_address_range((void*)arg1, sizeof(sigset_t), 1, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            return_value = sys_sigsuspend((const sigset_t *)arg1);
            break;
        case SYSCALL_SIGQUEUE:
            return_value = sys_sigqueue(arg1, arg2, (union sigval){arg3});
            break;
        case SYSCALL_IOCTL:
            asm volatile ("sti");
            return_value = sys_ioctl(arg1, arg2, (void *)arg3);
            break;
        default:
            return_value = -ENOSYS;
            break;
    }


    syscall_exit:
    #ifndef EXIT_AFFECTS_SYSCALLS
    CRIT_SEC_END
    #endif
    if (current_thread->in_critical_section) {
        kprintf("Exiting syscall %d with critical counter at %lu! Forcing to 0!\n", syscall_number, current_thread->in_critical_section);
    }
    current_thread->in_critical_section = 0;

    asm volatile ("cli;");

    memcpy(&current_thread->context, ctx, sizeof(mcontext_t) - (ctx->iret_frame.cs & 3 ? 0 : 2*sizeof(void *)));
    signal_dispatch_sa(current_process, current_thread);
    memcpy(ctx, &current_thread->context, sizeof(mcontext_t) - (ctx->iret_frame.cs & 3 ? 0 : 2*sizeof(void *)));

#ifdef SYSCALLS_RESCHEDULE
    reschedule();
#else
    if (current_process->do_cleanup) reschedule();
    if (current_process->is_stopped) reschedule();

    // sleep, waiting, ...
    // every syscall should be rescheduling on its own, but just in case
    if (current_thread->status != SCHED_RUNNING) reschedule();
#endif

    reload_pcb(current_process);

    // wont work because we do popa
    //return return_value;
    ctx->eax = return_value;
}