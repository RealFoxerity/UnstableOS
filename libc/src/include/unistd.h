#ifndef _UNISTD_H
#define _UNISTD_H

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#include "sys/types.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int close(int fd);
ssize_t write(int fd, const void * buf, size_t count);
ssize_t read (int fd, void * buf, size_t count);

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

pid_t getpid();
pid_t getppid();


long syscall(unsigned long syscall_number, ...);

#endif