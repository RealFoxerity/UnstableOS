#ifndef TLS_H
#define TLS_H
#include <limits.h>

#define ___PROGRAM_HEAP_VADDR (0x80000000) // base
#define PROGRAM_HEAP_VADDR ((void*)___PROGRAM_HEAP_VADDR) // base
#define PROGRAM_MAX_HEAP_SIZE (0x40000000) // 1GiB

#define __PROGRAM_TCB_SIZE 256 // assuming 256 bytes is enough for tcb
#define PROGRAM_DVT_SIZE (PAGESIZE)
#define PROGRAM_MAX_TLS_SIZE ((1<<15) - __PROGRAM_TCB_SIZE)
#define PROGRAM_TLS_VADDR (PROGRAM_HEAP_VADDR - (PROGRAM_MAX_TLS_SIZE + __PROGRAM_TCB_SIZE) * PTHREAD_THREADS_MAX)
#define PROGRAM_DVT_VADDR (PROGRAM_TLS_VADDR - PROGRAM_DVT_SIZE)
#define PROGRAM_TLS_BLUEPRINT_VADDR (PROGRAM_TLS_VADDR - PROGRAM_MAX_TLS_SIZE - __PROGRAM_TCB_SIZE - PROGRAM_DVT_SIZE)
#define PROGRAM_TLS_BLUEPRINT_TOP_VADDR (PROGRAM_TLS_VADDR - PROGRAM_DVT_SIZE - __PROGRAM_TCB_SIZE)

// slightly less than the theoretical maximum, that being around 1020
#define MAX_DTV_ENTRIES (1000)

struct tcb {
    struct tcb *self; // required to go from %gs to normal address (SysV ABI)
    void *dtv_ptr;
};
struct tcb * get_tcb();

// recopies blueprints and dvt
void reload_tls();
#endif