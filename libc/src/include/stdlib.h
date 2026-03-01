#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#define RAND_MAX 65536
#include <stdint.h>

uint32_t rand();
void srand(uint32_t seed);
long syscall(unsigned long syscall_number, ...);
void __attribute__((noreturn)) exit(long exit_code);
void __attribute__((noreturn)) abort();
int exec(const char * path);
void yield();

#define assert(cond) {\
    if (!(cond)) {\
        printf("Assertion `"#cond"` failed in %s()! [" __FILE__ ":" STR(__LINE__) "]\n", __func__);\
        abort();\
    }\
}

typedef size_t pid_t;

int waitpid(pid_t pid, int * status, int options);
pid_t fork();
pid_t getpid();
pid_t spawn(const char * path);
pid_t wait(int * wstatus);

#define WEXITSTATUS(wstatus) (wstatus & 0xFF)

//void malloc_prepare(void * heap_struct_start, void * heap_top);

#pragma clang diagnostic ignored "-Wignored-attributes" // clang doesn't yet support malloc(x) attribute syntax
void free(void * p);
void * __attribute__((malloc, malloc(free))) malloc(size_t size);


#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

unsigned long long strtoull(const char * start, char ** end_out);
unsigned long strtoul(const char * start, char ** end_out);
long long strtoll(const char * start, char ** end_out);
long strtol(const char * start, char ** end_out);

#endif