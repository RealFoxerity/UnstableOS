#include "tls.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

struct tcb * get_tcb() {
    uintptr_t __seg_gs * tcb_addr = NULL;
    return (struct tcb *)*tcb_addr;
}

// recopies blueprints and dvt
void reload_tls() {
    struct tcb * tcb = get_tcb();
    memcpy((void*)tcb - PROGRAM_MAX_TLS_SIZE, PROGRAM_TLS_BLUEPRINT_VADDR, PROGRAM_MAX_TLS_SIZE);
    // TODO: when doing dynamic TLS blocks, adjust DVT
}