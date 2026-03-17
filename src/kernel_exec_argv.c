#include "include/kernel.h"
#include "include/kernel_exec.h"
#include "include/kernel_sched.h"
#include "include/mm/kernel_memory.h"
#include "../libc/src/include/string.h"
#include "include/errno.h"
#include <stddef.h>

static char check_address(const void * address) {
    PAGE_TABLE_TYPE * pte = paging_get_pte(address);
    if (pte == NULL || (!(*pte & PTE_PDE_PAGE_USER_ACCESS) && current_process->ring != 0))
        return 0;
    return 1;
}

// TODO: (SMP) not thread safe
// TODO: check more thoroughly

static ssize_t vector_check(char * const* vec, size_t vec_size, size_t * count_out) {
    kassert(count_out);
    // allows the semi-nonposix NULL arguments and environment
    if (vec == NULL) {
        *count_out = 0;
        return vec_size;
    }

    size_t argc = 0;
    // enumerate and check the list to determine item count
    for (argc = 0; vec_size < ARG_MAX; argc++, vec_size += sizeof(const char *)) {
        if (!check_address(vec + argc)) return EFAULT;
        if (vec[argc] == NULL) break;
        if (!check_address(vec[argc])) return EFAULT;
    }
    vec_size += sizeof(const char *); // add the NULL pointer (see the System V ABI)
    // ran into the size limit before the guarding NULL
    if (vec[argc] != NULL) return EFAULT;
    if (vec_size > ARG_MAX) return E2BIG;

    // enumerate the strings - check if we can do strcpy
    for (int i = 0; i < argc; i++) {
        for (int j = 0; vec[i][j] != '\0'; j++, vec_size++) {
            if (!check_address(vec)) return EFAULT;
        }
        vec_size++; // add the null byte
        if (vec_size > ARG_MAX) return E2BIG;
    }
    *count_out = argc;
    return vec_size;
}

struct {
    int type;
    union {
        long val;
        void * ptr;
        void (*fcn)();
    } un;
} typedef auxv_t;

enum auxv_types {
    AT_NULL,
    AT_IGNORE,
    AT_EXECFD,
    AT_PHDR,
    AT_PHENT,
    AT_PHNUM,
    AT_PAGESZ,
    AT_BASE,
    AT_FLAGS,
    AT_ENTRY,
    AT_LIBPATH,
    AT_FPHW,
    AT_INTP_DEVICE,
    AT_INTP_INODE
};

ssize_t exec_safe_argv_dup(char * const* argv, char * const* envp, void * stack_top_addr, char ** stack_out) {
    if (argv == NULL) return EFAULT;
    if (envp == NULL) return EFAULT;
    if (!check_address(argv)) return EFAULT;
    if (!check_address(envp)) return EFAULT;

    kassert(stack_out);

    ssize_t argv_size = sizeof(unsigned long); // argc, null pointers are counted by vector_check
    argv_size += sizeof(auxv_t); // NULL auxv entry - we don't yet support any others

    size_t argc = 0;
    size_t envc = 0;
    size_t auxc = 0;

    argv_size = vector_check(argv, argv_size, &argc);
    if (argv_size < 0) return argv_size;

    argv_size = vector_check(envp, argv_size, &envc);
    if (argv_size < 0) return argv_size;

    char * stack_state = kalloc(argv_size);
    kassert(stack_state);
    memset(stack_state, 0, argv_size);

    ((unsigned long*)stack_state)[0] = argc;
    /* if moving on from memset
    ((char**)stack_state)[1 + argc] = NULL;
    ((char**)stack_state)[1 + argc + 1 + envc] = NULL;
    */
    ((auxv_t *)(((char **)stack_state)[1 + argc + 1 + envc + 1]))[auxc] = (auxv_t) {.type = AT_NULL};

    size_t end_off = 0;

    for (int i = 0; i < argc; i++) {
        end_off += strlen(argv[i]) + 1;
        kassert(end_off <= argv_size - (1 + argc + 1 + envc + 1)*sizeof(char**) - (auxc + 1)*sizeof(auxv_t));
        strcpy(stack_state + argv_size - end_off, argv[i]);
        ((char**)stack_state)[1 + i] = stack_top_addr - end_off;
    }

    for (int i = 0; i < envc; i++) {
        end_off += strlen(envp[i]) + 1;
        kassert(end_off <= argv_size - (1 + argc + 1 + envc + 1)*sizeof(char**) - (auxc + 1)*sizeof(auxv_t));
        strcpy(stack_state + argv_size - end_off, envp[i]);

        ((char**)stack_state)[1 + argc + 1 + i] = stack_top_addr - end_off;
    }

    *stack_out = stack_state;
    return argv_size;
}