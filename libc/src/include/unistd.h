#ifndef _UNISTD_H
#define _UNISTD_H

#define _POSIX_VERSION 202405L
#define _POSIX_CLOCK_SELECTION 202405L
#define _POSIX_NO_TRUNC 1
#define _POSIX_REALTIME_SIGNALS 202405L
#define _POSIX_SAVED_IDS 1
#define _POSIX_SPIN_LOCKS 202405L
#define _POSIX_THREAD_SAFE_FUNCTIONS 202405L
#define _POSIX_THREADS 202405L
#define _POSIX_TIMEOUTS 202405L
#define _POSIX_V7_ILP32_OFFBIG 1
#define _POSIX_V8_ILP32_OFFBIG 1


#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#include <stdint.h>
#include <stdio.h>

#include <sys/types.h>

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
int ftruncate(int fildes, off_t length);
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

int unlinkat(int fd, const char *path, int flag);
int unlink(const char *path);
int rmdir(const char *path);

int chdir(const char * path);
int chroot(const char * path);

pid_t fork();
pid_t _Fork();
pid_t spawn(const char * path, char * const* argv, char * const* envp);
int exec(const char * path);
int execv(const char * path, char * const* argv);
int execve(const char * path, char * const* argv, char * const* envp);

int execvp(const char * file, char * const* argv);
int execvpe(const char * file, char * const* argv, char * const* envp);

int execl(const char * path, const char * arg0, ...);
int execle(const char * path, const char * arg0, ...);
int execlp(const char * file, const char * arg0, ...);

uid_t getuid();
uid_t geteuid();
int getresuid(uid_t *__restrict ruid, uid_t *__restrict euid, uid_t *__restrict suid);

gid_t getgid();
gid_t getegid();
int getresgid(uid_t *__restrict rgid, uid_t *__restrict egid, uid_t *__restrict sgid);

int setgid(gid_t gid);
int setegid(gid_t gid);
int setregid(gid_t rgid, gid_t egid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
int setuid(uid_t uid);
int seteuid(uid_t uid);
int setreuid(uid_t ruid, uid_t euid);
int setresuid(uid_t ruid, uid_t euid, uid_t suid);

int getgroups(int gidsetsize, gid_t grouplist[]);
int setgroups(int gidsetsize, gid_t grouplist[]);

pid_t getpid();
pid_t gettid();
pid_t getppid();
pid_t getpgrp();

pid_t getpgid(pid_t pid);
pid_t getsid(pid_t pid);
pid_t setsid();
int   setpgid(pid_t pid, pid_t pgid);

unsigned sleep(unsigned seconds);
unsigned alarm(unsigned seconds);
int pause();

long syscall(unsigned long syscall_number, ...); // checks for pthread cancellability
long _syscall(unsigned long syscall_number, ...); // doesn't

void __attribute__((noreturn)) _exit(long exit_code);


extern char * optarg;
extern int opterr, optind, optopt;

int getopt(int argc, char * const argv[], const char *optstring);

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

int access(const char *path, int amode);
int faccessat(int fd, const char *path, int amode, int flag);

int link(const char *path1, const char *path2);
int linkat(int fd1, const char *path1, int fd2, const char *path2, int flag);

char *crypt(const char *key, const char *salt);

char * ttyname(int fildes);
int ttyname_r(int fildes, char *name, size_t namesize);
#endif