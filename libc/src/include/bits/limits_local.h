#ifndef _BITS_LIMITS_LOCAL_H
#define _BITS_LIMITS_LOCAL_H

// runtime invariant values

#define FILESIZEBITS 64

#define MAX_CANON 4096
#define MAX_INPUT MAX_CANON

// we don't have a limit at all
#define CHILD_MAX 0xFFFFFFFF

#define OPEN_MAX 128

#define PAGE_SIZE 4096
#define PAGESIZE PAGE_SIZE

#define RTSIG_MAX 32

#define SEM_NSEMS_MAX 256
#define SEM_VALUE_MAX ((unsigned long)-1)

#define PTHREAD_THREADS_MAX 256
#define PTHREAD_STACK_MIN 0

#define SIGQUEUE_MAX 32

#define ARG_MAX 0x4000 // 16KiB of argv

// due to the way we handle the ring buffer, it's actually PIPE_BUF - 1
#define PIPE_BUF 512

// pathname variable values
#define PATH_MAX 4096

#endif