#ifndef _UNISTD_H
#define _UNISTD_H

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#include <stdint.h>

#include <sys/types.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// if ICANON, putting this value into control chars disables the function
// why is this here? wouldn't it make more sense to have it in termios.h?
// but posix says so...
#define _POSIX_VDISABLE 0xF0

void swab(const void *__restrict src, void *__restrict dest, ssize_t nbytes);

int brk(void * addr);
void * sbrk(intptr_t increment);

int close(int fd);
ssize_t read (int fd, void * buf, size_t count);
ssize_t write(int fd, const void * buf, size_t count);
ssize_t pread (int fd, void * buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void * buf, size_t count, off_t offset);
void sync();

char *getcwd(char *buf, size_t size);

int pipe(int fildes[2]);
int pipe2(int fildes[2], int flags);

int isatty(int fildes); // termios.c
pid_t tcgetpgrp(int fildes); // termios.c
int tcsetpgrp(int fildes, pid_t pgid_id); // termios.c

off_t lseek(int fd, off_t offset, int whence);

int dup(int fd);
int dup2(int oldfd, int newfd);
int dup3(int oldfd, int newfd, int flag);

int chdir(const char * path);
int chroot(const char * path);

pid_t fork();
pid_t spawn(const char * path, char * const* argv, char * const* envp);
int exec(const char * path);
int execv(const char * path, char * const* argv);
int execve(const char * path, char * const* argv, char * const* envp);

int execvp(const char * file, char * const* argv);
int execvpe(const char * file, char * const* argv, char * const* envp);

int execl(const char * path, const char * arg0, ...);
int execle(const char * path, const char * arg0, ...);
int execlp(const char * file, const char * arg0, ...);

pid_t getpid();
pid_t gettid();
pid_t getppid();

pid_t getpgid(pid_t pid);
pid_t setsid();
int   setpgid(pid_t pid, pid_t pgid);

unsigned sleep(unsigned seconds);
unsigned alarm(unsigned seconds);
int pause();

long syscall(unsigned long syscall_number, ...);

void __attribute__((noreturn)) _exit(long exit_code);

#endif