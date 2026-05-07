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

int brk(void * addr);
void * sbrk(intptr_t increment);

int close(int fd);
ssize_t write(int fd, const void * buf, size_t count);
ssize_t read (int fd, void * buf, size_t count);

char *getcwd(char *buf, size_t size);

int pipe(int fildes[2]);

int isatty(int fildes); // termios.c
pid_t tcgetpgrp(int fildes); // termios.c
int tcsetpgrp(int fildes, pid_t pgid_id); // termios.c

off_t lseek(int fd, off_t offset, int whence);

int dup(int fd);
int dup2(int oldfd, int newfd);

int chdir(const char * path);
int chroot(const char * path);

pid_t fork();
pid_t spawn(const char * path, char * const* argv, char * const* envp);
int exec(const char * path);
int execv(const char * path, char * const* argv);
int execve(const char * path, char * const* argv, char * const* envp);

int execvp(const char * file, char * const* argv);
int execvpe(const char * file, char * const* argv, char * const* envp);

pid_t getpid();
pid_t gettid();
pid_t getppid();

unsigned sleep(unsigned seconds);
int pause();

long syscall(unsigned long syscall_number, ...);

void __attribute__((noreturn)) _exit(long exit_code);

#endif