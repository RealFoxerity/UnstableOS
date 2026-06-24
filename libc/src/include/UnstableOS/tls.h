#ifndef _UNSTABLEOS_TLS_H
#define _UNSTABLEOS_TLS_H

#include <sys/types.h>
#include <bits/limits_local.h>
struct process_control_block {
    pid_t pid;
    pid_t ppid;
    pid_t pgid;
    pid_t sid;
    uid_t uid;
    gid_t gid;

    // a way to keep track of available address ranges, 1 = used
    // PROGRAM_STACK_VADDR - i*PROGRAM_STACK_SIZE
    char thread_slots[PTHREAD_THREADS_MAX];
};
struct thread_control_block {
    struct thread_control_block *self; // required to go from %gs to normal address (SysV ABI)
    void *dtv_ptr;
    struct process_control_block *pcb;
    pid_t tid;
    unsigned int thread_slot;
};

#define MAX_DTV_ENTRIES (1000)
typedef struct
{
    unsigned long int ti_module;
    unsigned long int ti_offset;
} tls_index;
__attribute__ ((__regparm__ (1))) void * ___tls_get_addr (tls_index *ti);
struct thread_control_block * __tls_get_tcb();
#endif