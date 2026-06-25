#include <UnstableOS/tls.h>
#include <stddef.h>
#include <stdint.h>

struct thread_control_block * __tls_get_tcb() {
    uintptr_t __seg_gs * tcb_addr = NULL;
    return (struct thread_control_block *)*tcb_addr;
}

__attribute__ ((__regparm__ (1))) void * ___tls_get_addr (tls_index *ti) {
    uintptr_t __seg_gs * tcb_addr = NULL;
    struct thread_control_block * tcb =
        (struct thread_control_block *)*tcb_addr;

    unsigned long ** dvt = tcb->dtv_ptr;
    if (!ti) return NULL;
    if (ti->ti_module + 1 > MAX_DTV_ENTRIES) return NULL;
    return dvt[ti->ti_module] + ti->ti_offset;
}