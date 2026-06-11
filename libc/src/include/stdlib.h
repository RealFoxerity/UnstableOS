#ifndef _STDLIB_H
#define _STDLIB_H

#include "sys/types.h"

#include <stdint.h>
#define RAND_MAX (UINT32_MAX)

uint32_t rand();
void srand(uint32_t seed);
void __attribute__((noreturn)) exit(long exit_code);
void __attribute__((noreturn)) _Exit(long exit_code);
void __attribute__((noreturn)) abort();

void yield();

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
#endif