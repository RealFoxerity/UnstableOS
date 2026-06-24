#ifndef _LIMITS_H
#define _LIMITS_H

// numerical limits
#define CHAR_BIT  (sizeof(char) * 8)
#define WORD_BIT  (sizeof(int) * 8)
#define LONG_BIT  (sizeof(long) * 8)

#define UCHAR_MAX ((unsigned char)-1)

#define CHAR_MAX  (((unsigned char)1 << (CHAR_BIT - 1)) - 1)
#define CHAR_MIN  (-(char)CHAR_MAX)

#define SCHAR_MAX CHAR_MAX
#define SCHAR_MIN CHAR_MIN

#define INT_MAX   (((unsigned long)1 << (WORD_BIT - 1)) - 1)
#define INT_MIN   (-(long)INT_MAX)

#define UINT_MAX  ((unsigned int)-1)

#define LLONG_MAX (((unsigned long long)1 << (sizeof(long long) * 8 - 1)) - 1)
#define LLONG_MIN (-(long long)LLONG_MAX)

#define LONG_MAX  (((unsigned long)1 << (LONG_BIT - 1)) - 1)
#define LONG_MIN  (-(long)LONG_MAX)

#define ULONG_MAX  ((unsigned long)-1)

#define ULLONG_MAX ((unsigned long long)-1)

#define MB_LEN_MAX 1 // we don't yet support unicode :P

#define SHRT_MAX  (((unsigned short)1 << (sizeof(short) * 8 - 1)) - 1)
#define SHRT_MIN  (-(short)SHRT_MAX)

#define USHRT_MAX  ((unsigned short)-1)

#include "sys/types.h"
#define SSIZE_MAX  (((unsigned long long)1 << (sizeof(ssize_t) * 8 - 1)) - 1)
#define SSIZE_MIN  (-(long long)SSIZE_MAX)



#include "bits/limits_local.h"
#include "bits/limits_posix_minimums.h"
#endif