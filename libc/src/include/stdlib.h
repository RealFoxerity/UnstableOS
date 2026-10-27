#ifndef _STDLIB_H
#define _STDLIB_H

#include "sys/types.h"
#include "sys/wait.h" // posix requires WIF*
#include "fcntl.h" // posix requires O_*
#include <limits.h>
#include <stddef.h>

int abs(int i);

#define MB_CUR_MAX 4
#define RAND_MAX (INT_MAX)

int rand();
void srand(uint32_t seed);

int atexit(void (*func)());
void __attribute__((noreturn)) exit(long exit_code);
void __attribute__((noreturn)) _Exit(long exit_code);
void __attribute__((noreturn)) abort();

int system(const char *command);

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
float  strtof(const char *__restrict nptr, char **__restrict endptr);

// in strtod.c
double strtod(const char *__restrict nptr, char **__restrict endptr);
double atof(const char *str);


char * getenv(const char * name);
char * secure_getenv(const char *name);
int unsetenv(const char *name);
int putenv(char *string);
int setenv(const char *envname, const char *envval, int overwrite);
int clearenv();

int getsubopt(char **__restrict optionp, char * const *__restrict keylistp, char **__restrict valuep);

void qsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *));
void qsort_r(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *, void *), void *arg);

size_t mbstowcs(wchar_t *__restrict pwcs, const char *__restrict s, size_t n);
int mbtowc(wchar_t *__restrict pwc, const char *__restrict s, size_t n);
int mblen(const char *s, size_t n);

size_t wcstombs(char *__restrict s, const wchar_t *__restrict pwcs, size_t n);
int wctomb(char *s, wchar_t wchar);

char *mktemp(char *s);
char *mkdtemp(char *template);
int mkostemp(char *template, int flag);
int mkstemp(char *template);
#endif