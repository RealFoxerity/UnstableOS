#ifndef _UNSTABLEOS_SYSCALLS_H
#define _UNSTABLEOS_SYSCALLS_H

#define SYSCALL_INTERR 0xF0 // if changing, change crt0.s

enum syscalls {
    SYSCALL_EXIT = 0, // if changing, change crt0.s, exit(long exitcode)
    SYSCALL_ABORT = 1,
    SYSCALL_BRK, // same as linux, returns the current end on error
    SYSCALL_OPENAT,
    SYSCALL_CLOSE,
    SYSCALL_FCNTL,

    SYSCALL_DUP,
    SYSCALL_DUP3, // use flags -1 to act as dup2

    SYSCALL_MKDIR,
    //SYSCALL_CREATE, handled by OPEN
    SYSCALL_UNLINK,
    SYSCALL_UMASK,

    SYSCALL_READ,
    SYSCALL_WRITE,
    SYSCALL_PREAD,
    SYSCALL_PWRITE,

    SYSCALL_SEEK, // fd, const off_t * off, int whence, off_t * off_out; needed because 64 bit offsets
    SYSCALL_SYNC,

    SYSCALL_READDIR, // theoretically could be implemented in read()

    SYSCALL_PIPE2,

    SYSCALL_CHDIR,
    SYSCALL_CHROOT,

    SYSCALL_FSTAT,
    SYSCALL_FSTATAT,

    SYSCALL_MOUNT,
    SYSCALL_UMOUNT,

    SYSCALL_EXEC,
    SYSCALL_FORK,
    SYSCALL_SPAWN, // spawn a new process (fork() + exec())

    SYSCALL_GETPID,
    SYSCALL_GETPPID,
    SYSCALL_GETTID,
    SYSCALL_GETSID,
    SYSCALL_SETSID,
    SYSCALL_GETPGID, // getpgid(pid_t target_pid)
    SYSCALL_SETPGID, // setpgid(pid_t target_pid, pid_t target_pgid)

    SYSCALL_KILL,
    SYSCALL_TGKILL,

    SYSCALL_SIGACTION,
    SYSCALL_SIGRETURN,
    SYSCALL_SIGPROCMASK,
    SYSCALL_SIGPENDING,
    SYSCALL_SIGSUSPEND,
    SYSCALL_SIGQUEUE,

    SYSCALL_WAITPID, // the same as the waitpid() function
    SYSCALL_WAITID,

    SYSCALL_CREATE_THREAD, // create_thread(void (* entry_point)(void*), void * args)
    SYSCALL_EXIT_THREAD, // like exit() but for threads, no exitcode, has to be done via userspace (see libc/src/threads.c)

    SYSCALL_YIELD,

    SYSCALL_NANOSLEEP,
    SYSCALL_ALARM,
    SYSCALL_TIME,

    // different from the function since we can't easily return 64 bits: struct tms * buffer, clock_t * elapsed
    // we could do 32 bits, but then the 2038 problem comes to bite us
    SYSCALL_TIMES,

    SYSCALL_IOCTL,

    SYSCALL_FUTEX,
    SYSCALL_SEM_INIT, // semaphore_t sem_init(int initial val)
    SYSCALL_SEM_POST, // sem_post(int semaphore id)
    SYSCALL_SEM_WAIT, // sem_wait(int semaphore id)
    SYSCALL_SEM_DESTROY,
};

#endif