#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <UnstableOS/syscalls.h>
#include <UnstableOS/tls.h>
#include <sys/ioctl.h>

#include <assert.h>
#include <endian.h>

#include <fcntl.h>

void swab(const void *restrict src, void *restrict dest, ssize_t nbytes) {
    if (nbytes < 2) return;
    for (size_t i = 0; i < nbytes/2; i++) {
        ((uint16_t*)dest)[i] = htobe16(((uint16_t*)src)[i]);
    }
}

int brk(void * addr) {
    void * old_break = (void *)syscall(SYSCALL_BRK, NULL);
    void * new_break = (void *)syscall(SYSCALL_BRK, addr);

    if (new_break == old_break) {
        ___set_errno(-ENOMEM);
        return -1;
    }
    return 0;
}

void * sbrk(intptr_t increment) {
    void * old_break = (void *)syscall(SYSCALL_BRK, NULL);
    void * new_break = (void *)syscall(SYSCALL_BRK, old_break + increment);

    if (new_break == old_break) {
        ___set_errno(-ENOMEM);
        return (void *)-1;
    }
    return old_break;
}

int close(int fd) {
    int ret = syscall(SYSCALL_CLOSE, fd);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

ssize_t read (int fd, void * buf, size_t count) {
    ssize_t ret = syscall(SYSCALL_READ, fd, buf, count);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

ssize_t write(int fd, const void * buf, size_t count) {
    ssize_t ret = syscall(SYSCALL_WRITE, fd, buf, count);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

ssize_t pread (int fd, void * buf, size_t count, off_t offset) {
    ssize_t ret = syscall(SYSCALL_READ, fd, buf, count, offset);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
ssize_t pwrite(int fd, const void * buf, size_t count, off_t offset) {
    ssize_t ret = syscall(SYSCALL_WRITE, fd, buf, count, offset);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

void sync() {
    syscall(SYSCALL_SYNC);
}

int pipe2(int fildes[2], int flags) {
    int ret = syscall(SYSCALL_PIPE2, fildes, flags);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int pipe(int fildes[2]) {
    return pipe2(fildes, 0);
}

off_t lseek(int fd, off_t offset, int whence) {
    off_t out = 0;
    off_t ret = syscall(SYSCALL_SEEK, fd, &offset, whence, &out);
    if (ret < 0) {
        ___set_errno((long)-ret);
        return -1;
    }
    return out;
}

int dup(int fd) {
    int ret = syscall(SYSCALL_DUP, fd);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int dup2(int oldfd, int newfd) {
    return dup3(oldfd, newfd, -1);
}

int dup3(int oldfd, int newfd, int flag) {
    int ret = syscall(SYSCALL_DUP3, oldfd, newfd, flag);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int unlink(const char *path) {
    return unlinkat(AT_FDCWD, path, 0);
}

int rmdir(const char *path) {
    return unlinkat(AT_FDCWD, path, AT_REMOVEDIR);
}

int chdir(const char * path) {
    int ret = syscall(SYSCALL_CHDIR, path);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int chroot(const char * path) {
    int ret = syscall(SYSCALL_CHROOT, path);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

pid_t _Fork() {
    pid_t ret = syscall(SYSCALL_FORK);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

pid_t fork() {
    extern void __atfork_handler(pid_t new_pid);
    __atfork_handler(-1);
    pid_t new = _Fork();

    // in case the parent handlers are supposed to restore some context
    if (new == -1) new = 1;
    __atfork_handler(new);
    return new;
}

pid_t spawn(const char * path, char * const* argv, char * const* envp) {
    pid_t ret = syscall(SYSCALL_SPAWN, path, argv, envp);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

pid_t getpid() {
    //return syscall(SYSCALL_GETPID);
    return __tls_get_tcb()->pcb->pid;
}
pid_t gettid() {
    //return syscall(SYSCALL_GETTID);
    return __tls_get_tcb()->tid;

}
pid_t getppid() {
    //return syscall(SYSCALL_GETPPID);
    return __tls_get_tcb()->pcb->pid;
}

pid_t getpgid(pid_t pid) {
    struct thread_control_block * tcb = __tls_get_tcb();
    if (pid == 0 || pid == tcb->pcb->pid)
        return tcb->pcb->pgid;

    pid_t ret = syscall(SYSCALL_GETPGID, pid);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

pid_t getsid(pid_t pid) {
    struct thread_control_block * tcb = __tls_get_tcb();
    if (pid == 0 || pid == tcb->pcb->pid)
        return tcb->pcb->sid;

    pid_t ret = syscall(SYSCALL_GETSID, pid);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

pid_t setsid() {
    pid_t ret = syscall(SYSCALL_SETSID);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int setpgid(pid_t pid, pid_t pgid) {
    int ret = syscall(SYSCALL_SETPGID, pid, pgid);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

unsigned sleep(unsigned seconds) {
    struct timespec actual = {0};
    struct timespec waited = {.tv_sec = seconds, .tv_nsec = 0};

    long ret = syscall(SYSCALL_NANOSLEEP, &waited, &actual);
    if (ret < 0) {
        ___set_errno(-ret);
        return ret;
    }

    return actual.tv_sec;
}
unsigned alarm(unsigned seconds) {
    return (unsigned)syscall(SYSCALL_ALARM, seconds);
}
int pause() {
    return sigpause(0);
}

static long _vsyscall(unsigned long syscall_number, va_list args) { // interrupt handler in kernel_syscall.c

    unsigned long arg1 = va_arg(args, unsigned long), arg2 = va_arg(args, unsigned long),
                    arg3 = va_arg(args, unsigned long), arg4 = va_arg(args, unsigned long),
                      arg5 = va_arg(args, unsigned long); // basically just used by futex
    va_end(args);

    long out = (long)syscall_number;
    asm volatile (
        "pushl %1;"
        "pushl %2;"
        "int $" STR(SYSCALL_INTERR)"\n\t"
        "addl $0x8, %%esp"
        :"+a" (out)
        :"m"(arg5), "m"(arg4), "D"(arg1), "S"(arg2), "d"(arg3)
    );
    return out;
}

#include <pthread.h>
long syscall(unsigned long syscall_number, ...) {
    va_list args;
    va_start(args, syscall_number);
    pthread_testcancel();
    return _vsyscall(syscall_number, args);
}
long _syscall(unsigned long syscall_number, ...) {
    va_list args;
    va_start(args, syscall_number);
    return _vsyscall(syscall_number, args);
}
int ioctl(int fildes, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    unsigned long arg = va_arg(args, unsigned long);
    va_end(args);

    long ret = syscall(SYSCALL_IOCTL, fildes, request, arg);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

int ftruncate(int fildes, off_t length) {
    int ret = syscall(SYSCALL_TRUNC, fildes, length);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}