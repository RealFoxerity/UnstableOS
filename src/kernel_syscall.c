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
#include "sys/mman.h"
#include "lowlevel.h"

#define kprintf(fmt, ...) kprintf("Kernel Routines: "fmt, ##__VA_ARGS__)

extern void clear_screen_fatal(); // kernel_interrupts.c

#define SYSCALL_PANIC_TEXT " #### RING 2 INDUCED PANIC; HALTING #### "


void kernel_syscall_dispatcher(__gregcontext_t * ctx);
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

void kernel_syscall_dispatcher(__gregcontext_t * ctx) {
    kassert(current_process);
    kassert(current_thread);

    if (current_process->do_cleanup) reschedule();
    if (current_process->is_stopped) reschedule();

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
    arg5 = ((long*)(ctx->iret_frame.sp))[1],
    arg6 = ((long*)(ctx->iret_frame.sp))[2];

    long return_value = -ENOSYS;

    #ifndef EXIT_AFFECTS_SYSCALLS
    CRIT_SEC_START
    #endif
    siginfo_t exited_child_status;

    asm volatile ("sti;");

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
            asm volatile("cli");
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
            VM_LOCK(arg2);
            if (!paging_check_address_range((void*)arg2, (size_t)arg3, 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            return_value = sys_read(arg1, (void*)arg2, (size_t)arg3);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_WRITE:
            VM_LOCK(arg2);
            if (!paging_check_address_range((const void*)arg2, (size_t)arg3, 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            return_value =  sys_write(arg1, (const void*)arg2, (size_t)arg3);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_PREAD:
            VM_LOCK(arg2);
            if (!paging_check_address_range((void*)arg2, (size_t)arg3, 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            return_value = sys_pread(arg1, (void*)arg2, (size_t)arg3, arg4);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_PWRITE:
            VM_LOCK(arg2);
            if (!paging_check_address_range((const void*)arg2, (size_t)arg3, 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            return_value = sys_pwrite(arg1, (const void*)arg2, (size_t)arg3, arg4);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_TRUNC:
            VM_LOCK(arg2);
            if (!paging_check_address_range((const void*)arg2, sizeof(off_t), 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            off_t new_size = *(off_t*)arg2;
            VM_UNLOCK(arg2);
            return_value = sys_trunc(arg1, new_size);
            break;
        case SYSCALL_FCNTL:
            return_value = sys_fcntl(arg1, arg2, arg3);
            break;
        case SYSCALL_SYNC:
            return_value = 0;
            extern void hd_cache_flush();
            hd_cache_flush();
            break;
        case SYSCALL_PIPE2:
            VM_LOCK(arg1);
            if (!paging_check_address_range((int *)arg1, 2*sizeof(int), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg1);
                break;
            }
            return_value = sys_pipe((int *)arg1, arg2);
            VM_UNLOCK(arg1);
            break;
        case SYSCALL_DUP:
            return_value = sys_dup(arg1);
            break;
        case SYSCALL_DUP3:
            return_value = sys_dup3(arg1, arg2, arg3);
            break;
        case SYSCALL_RENAMEAT:
            return_value = sys_renameat(arg1, (const char *)arg2, arg3, (const char *)arg4);
            break;
        case SYSCALL_UNLINKAT:
            return_value = sys_unlinkat(arg1, (const char *)arg2, arg3);
            break;
        case SYSCALL_SEEK:
            VM_LOCK(arg2);
            VM_LOCK(arg4);
            if (!paging_check_address_range((off_t*)arg2, sizeof(off_t), 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                VM_UNLOCK(arg4);
                break;
            }
            if (!paging_check_address_range((off_t*)arg4, sizeof(off_t), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                VM_UNLOCK(arg4);
                break;
            }
            off_t in = *(off_t*) arg2;
            VM_UNLOCK(arg2);
            off_t out = sys_seek(arg1, in, arg3);
            if (out >= 0) {
                *(off_t*)arg4 = out;
                return_value = 0;
            } else {
                return_value = (long)out; // negative errors
            }
            VM_UNLOCK(arg4);
            break;
        case SYSCALL_READDIR:
            VM_LOCK(arg2);
            if (!paging_check_address_range((void*)arg2, arg3, 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            return_value = sys_readdir(arg1, (void*)arg2, arg3);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_SEM_INIT:
            asm volatile("cli"); // TODO: rewrite to be thread safe
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
            return_value = sys_futex((uint32_t *)arg1, arg2, arg3, arg4, (struct timespec *)arg5, (clockid_t)arg6);
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

            kernel_sem_post(current_process, arg1);
            return_value = 0;
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
            asm volatile("cli");
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
            return_value = sys_mount((const char*)arg1, (const char*)arg2, (unsigned char)arg3, (unsigned short)arg4);
            break;
        case SYSCALL_UMOUNT:
            return_value = sys_umount((const char*)arg1);
            break;
        case SYSCALL_FACCESSAT:
            return_value = sys_faccessat(arg1, (const char *)arg2, arg3, arg4);
            break;
        case SYSCALL_OPENAT:
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
            return_value = sys_close(arg1);
            break;
        case SYSCALL_CHDIR:
            return_value = sys_chdir((const char *)arg1);
            break;
        case SYSCALL_CHROOT:
            return_value = sys_chroot((const char *)arg1);
            break;
        case SYSCALL_EXEC:
            rw_spinlock_acquire_read(&current_process->vm_lock);
            return_value = sys_execve((const char *)arg1, (char * const*)arg2, (char * const*)arg3);
            rw_spinlock_release_read(&current_process->vm_lock);
            break;
        case SYSCALL_SPAWN:
            rw_spinlock_acquire_read(&current_process->vm_lock);
            return_value = sys_spawn((const char *)arg1, (char * const*)arg2, (char * const*)arg3);
            rw_spinlock_release_read(&current_process->vm_lock);
            break;
        case SYSCALL_FORK:
            rw_spinlock_acquire_read(&current_process->vm_lock);
            return_value = sys_fork(ctx);
            rw_spinlock_release_read(&current_process->vm_lock);
            break;
        case SYSCALL_WAITPID:
            VM_LOCK(arg2);
            if ((int*)arg2 != NULL) {
                if (!paging_check_address_range((int*)arg2, sizeof(int), 1, in_kernel)) {
                    return_value = -EFAULT;
                    VM_UNLOCK(arg2);
                    break;
                }
            }
            return_value = sys_waitpid(arg1, (int*)arg2, arg3);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_WAITID:
            VM_LOCK(arg3);
            if (!paging_check_address_range((siginfo_t*)arg3, sizeof(siginfo_t), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg3);
                break;
            }
            return_value = sys_waitid(arg1, arg2, (siginfo_t*)arg3, arg4);
            VM_UNLOCK(arg3);
            break;
        case SYSCALL_FSTAT:
            VM_LOCK(arg2);
            if (!paging_check_address_range((void*)arg2, sizeof(struct stat), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            return_value = sys_fstat(arg1, (struct stat *)arg2);
            VM_UNLOCK(arg2);
            break;

        case SYSCALL_FSTATAT:
            VM_LOCK(arg3);
            if (!paging_check_address_range((void*)arg3, sizeof(struct stat), 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg3);
                break;
            }
            return_value = sys_fstatat(arg1, (const char*) arg2, (struct stat *)arg3, arg4);
            VM_UNLOCK(arg3);
            break;

        case SYSCALL_ALARM:
            return_value = (long)sys_alarm((unsigned)arg1);
            break;
        case SYSCALL_TIME:
            VM_LOCK(arg1);
            if (!paging_check_address_range((void*)arg1, sizeof(time_t), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg1);
                break;
            }
            *(time_t*)arg1 = system_time_sec;
            return_value = 0;
            VM_UNLOCK(arg1);
            break;

        case SYSCALL_CLOCK_GETRES:
            VM_LOCK(arg2);
            if ((void*)arg2 != NULL && !paging_check_address_range((void*)arg2, sizeof(struct timespec), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            switch (arg1) {
                case CLOCK_MONOTONIC:
                case CLOCK_REALTIME:
                    if (arg2) {
                        *(struct timespec*)arg2 = (struct timespec) {
                            .tv_nsec = 1000000000 / RTC_TIMER_RESOLUTION_HZ};
                    }
                    return_value = 0;
                    break;
                default:
                    return_value = -EINVAL;
            }
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_CLOCK_GETTIME:
            VM_LOCK(arg2);
            if (!paging_check_address_range((void*)arg2, sizeof(struct timespec), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            struct timespec * ts = (struct timespec *) arg2;
            switch (arg1) {
                case CLOCK_MONOTONIC:
                    ts->tv_sec = uptime_clicks / RTC_TIMER_RESOLUTION_HZ;
                    ts->tv_nsec = (long)(uptime_clicks % RTC_TIMER_RESOLUTION_HZ) * RTC_TIME_RESOLUTION_USEC * 1000;
                    return_value = 0;
                    break;
                case CLOCK_REALTIME:
                    ts->tv_sec = system_time_sec;
                    ts->tv_nsec = (long)(uptime_clicks % RTC_TIMER_RESOLUTION_HZ) * RTC_TIME_RESOLUTION_USEC * 1000;
                    return_value = 0;
                    break;
                default:
                    return_value = -EINVAL;
            }
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_CLOCK_SETTIME:
            VM_LOCK(arg2);
            if (!paging_check_address_range((void*)arg2, sizeof(struct timespec), 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            extern void rtc_set_time(time_t epoch);
            struct timespec * new_time = (struct timespec *) arg2;
            switch (arg1) {
                case CLOCK_REALTIME:
                    rtc_set_time(new_time->tv_sec);
                    return_value = 0;
                    break;
                case CLOCK_MONOTONIC:
                default:
                    return_value = -EINVAL;
            }
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_CLOCK_NANOSLEEP:
            VM_LOCK(arg3);
            VM_LOCK(arg4);
            if (!paging_check_address_range((void*)arg3, sizeof(struct timespec), 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg3);
                VM_UNLOCK(arg4);
                break;
            }
            if ((struct timespec *)arg4 != NULL && !paging_check_address_range((void*)arg4, sizeof(struct timespec), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg3);
                VM_UNLOCK(arg4);
                break;
            }
            return_value = sys_clock_nanosleep(current_process, current_thread, (clockid_t)arg1, arg2, *(struct timespec*)arg3, (struct timespec*)arg4);
            VM_UNLOCK(arg3);
            VM_UNLOCK(arg4);
            break;

        case SYSCALL_TIMES:
            VM_LOCK(arg1);
            VM_LOCK(arg2);

            if (!paging_check_address_range((void*)arg1, sizeof(struct tms), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg1);
                VM_UNLOCK(arg2);
                break;
            }
            if (!paging_check_address_range((void*)arg2, sizeof(clock_t), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg1);
                VM_UNLOCK(arg2);
                break;
            }
            *(struct tms*)arg1 = (struct tms) {
                .tms_utime = current_process->user_clicks,
                .tms_stime = current_process->system_clicks,
                .tms_cutime = current_process->dead_user_clicks,
                .tms_cstime = current_process->dead_system_clicks,
            };
            *(clock_t *)arg2 = uptime_clicks;
            VM_UNLOCK(arg1);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_KILL:
            return_value = sys_kill(arg1, (int)arg2);
            break;
        case SYSCALL_TGKILL:
            return_value = sys_tgkill(arg1, arg2, arg3);
            break;
        case SYSCALL_SIGACTION:
            VM_LOCK(arg2);
            VM_LOCK(arg3);

            if (!paging_check_address_range((void*)arg2, sizeof(struct sigaction), 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg3);
                VM_UNLOCK(arg2);
                break;
            }

            if ((struct sigaction *)arg3 != NULL && !paging_check_address_range((void*)arg3, sizeof(struct sigaction), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg3);
                VM_UNLOCK(arg2);
                break;
            }
            return_value = sys_sigaction(arg1, (struct sigaction *)arg2, (struct sigaction *)arg3);
            VM_UNLOCK(arg3);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_SIGRETURN:
            VM_LOCK(ctx);
            sys_sigreturn(ctx);
            VM_UNLOCK(ctx);
            break;
        case SYSCALL_SIGPROCMASK:
            VM_LOCK(arg2);
            if ((struct sigaction *)arg2 != NULL && !paging_check_address_range((void*)arg2, sizeof(sigset_t), 0, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg2);
                break;
            }
            VM_LOCK(arg3);
            if ((struct sigaction *)arg3 != NULL && !paging_check_address_range((void*)arg3, sizeof(sigset_t), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg3);
                VM_UNLOCK(arg2);
                break;
            }
            return_value = sys_sigprocmask(arg1, (const sigset_t *)arg2, (sigset_t *)arg3);
            VM_UNLOCK(arg3);
            VM_UNLOCK(arg2);
            break;
        case SYSCALL_SIGPENDING:
            VM_LOCK(arg1);
            if (!paging_check_address_range((void*)arg1, sizeof(sigset_t), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg1);
                break;
            }
            *(sigset_t *)arg1 = current_process->sa_pending;
            return_value = 0;
            VM_UNLOCK(arg1);
            break;
        case SYSCALL_SIGSUSPEND:
            VM_LOCK(arg1);
            if (!paging_check_address_range((void*)arg1, sizeof(sigset_t), 1, in_kernel)) {
                return_value = -EFAULT;
                VM_UNLOCK(arg1);
                break;
            }
            return_value = sys_sigsuspend((const sigset_t *)arg1);
            VM_UNLOCK(arg1);
            break;
        case SYSCALL_SIGQUEUE:
            return_value = sys_sigqueue(arg1, arg2, (union sigval){arg3});
            break;
        case SYSCALL_IOCTL:
            rw_spinlock_acquire_read(&current_process->vm_lock);
            return_value = sys_ioctl(arg1, arg2, (void *)arg3);
            rw_spinlock_release_read(&current_process->vm_lock);
            break;



        case SYSCALL_MMAP:
            if (!paging_check_address_range((void*)arg6, sizeof(off_t), 0, in_kernel)) {
                return_value = -EFAULT;
                break;
            }
            return_value = (long)sys_mmap((void*)arg1, arg2, arg3, arg4, arg5, *(off_t*)arg6);
            break;
        case SYSCALL_MUNMAP:
            return_value = sys_munmap((void*)arg1, arg2);
            break;
        case SYSCALL_MPROTECT:
            return_value = sys_mprotect((void*)arg1, arg2, arg3);
            break;


        case SYSCALL_GETUID:
            return_value = (long)current_process->uid;
            break;
        case SYSCALL_GETEUID:
            return_value = (long)current_process->euid;
            break;
        case SYSCALL_GETSUID:
            return_value = (long)current_process->suid;
            break;
        case SYSCALL_GETGID:
            return_value = (long)current_process->gid;
            break;
        case SYSCALL_GETEGID:
            return_value = (long)current_process->egid;
            break;
        case SYSCALL_GETSGID:
            return_value = (long)current_process->sgid;
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


    reload_pcb(current_process);
    // here because we need to modify the context going into signal_dispatch_sa
    // lower parts need to be under cli because otherwise the eax assignment blows it up
    // reschedule directly goes into the signal handler, so no need to worry then

    if (syscall_number != SYSCALL_SIGRETURN) {
        // now time to handle EINTR and SA_RESTART
        if (return_value == -EINTR && (
            (
                current_thread->sa_to_be_handled &&
                current_process->sa_handlers[current_thread->sa_to_be_handled - 1].sa_flags & SA_RESTART
            ) || current_process->is_stopped
        )) {
            switch (syscall_number) {
                // add other cases where EINTR is not meant to be SA_RESTARTed
                case SYSCALL_SIGSUSPEND:
                    ctx->eax = return_value;
                    break;
                default:
                    ctx->iret_frame.ip -= 2; // int $0xF0 -> cd f0
            }
        } else {
            ctx->eax = return_value;
        }
    }

    asm volatile ("cli;");

    if (current_thread->sa_to_be_handled) {
        if (fxsave_available)
            asm volatile(
                "fxsave %0"
                ::"m"(current_thread->fpu_context)
            );
        else
            asm volatile(
                "fnsave %0"
                ::"m"(current_thread->fpu_context)
            );
        memcpy(&current_thread->context, ctx, sizeof(__gregcontext_t) - (ctx->iret_frame.cs & 3 ? 0 : 2*sizeof(void *)));
        signal_dispatch_sa(current_process, current_thread);
        memcpy(ctx, &current_thread->context, sizeof(__gregcontext_t) - (ctx->iret_frame.cs & 3 ? 0 : 2*sizeof(void *)));
    }
#ifdef SYSCALLS_RESCHEDULE
    reschedule();
#else
    if (current_process->do_cleanup) reschedule();
    if (current_process->is_stopped) reschedule();

    // sleep, waiting, ...
    // every syscall should be rescheduling on its own, but just in case
    if (current_thread->status != SCHED_RUNNING) reschedule();
#endif
}