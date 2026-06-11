#ifndef _ALLOCA_H
#define _ALLOCA_H

#include <stddef.h>

#undef alloca
extern void * alloca(size_t size);
#define alloca(size) __builtin_alloca(size)

#endif
