#ifndef _LIMITS_H
#define _LIMITS_H


// runtime invariant values

// we don't have a limit at all
#define CHILD_MAX 0xFFFFFFFF

#define OPEN_MAX 128

#define PAGE_SIZE 4096
#define PAGESIZE PAGE_SIZE

#define RTSIG_MAX 32

#define SEM_NSEMS_MAX 128
#define SEM_VALUE_MAX ((1<<(sizeof(unsigned long) * 8)) - 1)

#define PTHREAD_THREADS_MAX 256

#define SIGQUEUE_MAX 32

#define ARG_MAX 0x4000 // 16KiB of argv

// due to the way we handle the ring buffer, it's actually PIPE_BUF - 1
#define PIPE_BUF 512

// pathname variable values
#define PATH_MAX 4096

// numerical limits
#define CHAR_BIT  (sizeof(char) * 8)
#define WORD_BIT  (sizeof(int) * 8)
#define LONG_BIT  (sizeof(long) * 8)

#define UCHAR_MAX (1 << CHAR_BIT - 1)

#define CHAR_MAX  ((1 << (CHAR_BIT - 1)) - 1)
#define CHAR_MIN  (-CHAR_MAX)

#define SCHAR_MAX CHAR_MAX
#define SCHAR_MIN CHAR_MIN

#define INT_MAX   ((1 << (WORD_BIT - 1)) - 1)
#define INT_MIN   (-INT_MAX)

#define UINT_MAX  (1 << WORD_BIT - 1)

#define LLONG_MAX (((unsigned long long)1 << (sizeof(long long) * 8 - 1)) - 1)
#define LLONG_MIN (-LLONG_MAX)

#define LONG_MAX  (((unsigned long)1 << (LONG_BIT - 1)) - 1)
#define LONG_MIN  (-LONG_MAX)

#define ULONG_MAX  ((1 << LONG_BIT) - 1)

#define UULONG_MAX (((unsigned long long)1 << (sizeof(unsigned long long) * 8)) - 1)

#define MB_LEN_MAX 1 // we don't yet support unicode :P

#define SHRT_MAX  ((1 << (sizeof(short) * 8 - 1)) - 1)
#define SHRT_MIN  (-SHRT_MAX)

#define USHRT_MAX  ((1 << (sizeof(unsigned short) * 8)) - 1)

#include "types.h" // ssize_t
#define SSIZE_MAX  (((unsigned long long)1 << (sizeof(ssize_t) * 8 - 1)) - 1)
#define SSIZE_MIN  (-SSIZE_MAX)

#endif