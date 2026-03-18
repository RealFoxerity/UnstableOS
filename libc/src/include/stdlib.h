#ifndef _STDLIB_H
#define _STDLIB_H

#include "sys/types.h"

#include "sys/wait.h"

#define RAND_MAX 65536
#include <stdint.h>

uint32_t rand();
void srand(uint32_t seed);
void __attribute__((noreturn)) exit(long exit_code);
void __attribute__((noreturn)) _Exit(long exit_code);
void __attribute__((noreturn)) abort();

void yield();

#define assert(cond) {\
    if (!(cond)) {\
        printf("Assertion `"#cond"` failed in %s()! [" __FILE__ ":" STR(__LINE__) "]\n", __func__);\
        abort();\
    }\
}

//void malloc_prepare(void * heap_struct_start, void * heap_top);

#pragma clang diagnostic ignored "-Wignored-attributes" // clang doesn't yet support malloc(x) attribute syntax
void free(void * p);
void * __attribute__((malloc, malloc(free))) malloc(size_t size);
void * __attribute__((malloc, malloc(free))) calloc(size_t size);


#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

// in string.c
long long atoll(const char * nptr);
long atol(const char * nptr);
int atoi(const char * nptr);
unsigned long long strtoull(const char * start, char ** end_out);
unsigned long strtoul(const char * start, char ** end_out);
long long strtoll(const char * start, char ** end_out);
long strtol(const char * start, char ** end_out);

char * getenv(const char * name);
#endif