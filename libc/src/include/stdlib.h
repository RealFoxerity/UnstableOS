#ifndef _STDLIB_H
#define _STDLIB_H

#include "sys/types.h"
#include "sys/wait.h" // posix requires WIF*
#include "fcntl.h" // posix requires O_*
#include <limits.h>

int abs(int i);

#define RAND_MAX (INT_MAX)

int rand();
void srand(uint32_t seed);

int atexit(void (*func)());
void __attribute__((noreturn)) exit(long exit_code);
void __attribute__((noreturn)) _Exit(long exit_code);
void __attribute__((noreturn)) abort();

//void malloc_prepare(void * heap_struct_start, void * heap_top);

#pragma clang diagnostic ignored "-Wignored-attributes" // clang doesn't yet support malloc(x) attribute syntax
void free(void * p);
void * __attribute__((malloc, malloc(free))) malloc(size_t size);
void * __attribute__((malloc, malloc(free))) calloc(size_t nelem, size_t elsize);
void * realloc(void * p, size_t size);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

// in string.c
long long atoll(const char * nptr);
long atol(const char * nptr);
int atoi(const char * nptr);

// in stdlib_strto.c
unsigned long long strtoull(const char * __restrict start, char ** __restrict end_out, int base);
unsigned      long strtoul (const char * __restrict start, char ** __restrict end_out, int base);
         long long strtoll (const char * __restrict start, char ** __restrict end_out, int base);
              long strtol  (const char * __restrict start, char ** __restrict end_out, int base);

char * getenv(const char * name);
char * secure_getenv(const char *name);
int unsetenv(const char *name);
int putenv(char *string);
int setenv(const char *envname, const char *envval, int overwrite);

int getsubopt(char **restrict optionp, char * const *restrict keylistp, char **restrict valuep);
#endif