#include "include/stdlib.h"
#include "include/unistd.h"
#include "../../src/include/kernel.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "include/string.h"
#include "include/errno.h"

static uint32_t ___internal_rand_state = 1;

uint32_t rand() {
    ___internal_rand_state = ___internal_rand_state * 1103515245 + 12345;
    return (uint32_t) (___internal_rand_state/(RAND_MAX*2)) % RAND_MAX;
}

void srand(uint32_t seed) {___internal_rand_state = seed;}

void exit(long exit_code) {
    syscall(SYSCALL_EXIT, exit_code);
    __builtin_unreachable();
}

void _exit(long exit_code) {
    exit(exit_code);
}

void _Exit(long exit_code) {
    exit(exit_code);
}

void abort() {
    syscall(SYSCALL_ABORT);
    __builtin_unreachable();
}


void yield() {
    syscall(SYSCALL_YIELD);
}

pid_t wait(int * wstatus) {
    return waitpid(-1, wstatus, 0);
}
pid_t waitpid(pid_t pid, int * wstatus, int options) {
    pid_t ret = syscall(SYSCALL_WAITPID, pid, wstatus, options);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

extern char ** environ;
char * getenv(const char * name) {
    for (int i = 0; environ[i] != NULL; i++) {
        if (strlen(name) == strlen(environ[i]) && strcmp(name, environ[i]) == 0) {
            for (int j = 0; environ[i][j] != '\0'; j++) {
                if (environ[i][j] == '=') return &environ[i][j + 1];
            }
        }
    }
    return NULL;
}