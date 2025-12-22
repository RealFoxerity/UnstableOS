#include <stddef.h>
#include "include/elf.h"
#include "include/kernel.h"
#include "include/mm/kernel_memory.h"
#include "include/kernel_sched.h"
#include "../libc/src/include/endian.h"

#define kprintf(fmt, ...) kprintf("ELF loader: "fmt, ##__VA_ARGS__)

struct program load_elf(void * start, size_t size) { // returns VIRTUAL address of page directory for the elf's new address space
    if (start == NULL) return (struct program){0};
    // assumes kernel's address space (aka. data available in all address spaces)
    // assumes program and thread stacks are above heap, and that the program cannot load anything between the heap and stacks

    if (!check_elf(start, size)) return (struct program){0};

    struct elf_header * ehdr = start;

    // architecture dependant things that shouldn't be in check_elf
    if (ehdr->elf_header_version != ELF_HEADER_VERSION || ehdr->elf_version != ELF_VERSION) return (struct program){0};
    if (ehdr->arch_isa != ELF_ISA_X86 || ehdr->arch != ELF_ARCH_32) return (struct program){0};

    char found_loadable = 0;
    struct program_header * PH;
    size_t needed_memory = PROGRAM_STACK_SIZE + PROGRAM_KERNEL_STACK_SIZE + PROGRAM_HEAP_START_SIZE;

    for (int i = 0; i < ehdr->program_header_entry_count; i++) {
        PH = start + ehdr->program_header_table_offset + i*ehdr->program_header_table_entry_size;
        if (PH->type == ELF_PHT_LOAD) {
            found_loadable = 1;
            if ((void *)(unsigned long)PH->vaddr < kernel_mem_top) return (struct program){0}; // obv can't load into the kernel and the program probably wouldn't run without a loadable segment
            if ((void*)(unsigned long)PH->vaddr + PH->size_memory > PROGRAM_HEAP_VADDR) return (struct program){0}; // same but stacks and heap
            needed_memory += PH->size_memory;
        }
    }
    if (!found_loadable) return (struct program){0}; // useless trying to load an ELF file without any loadable segments
    if (needed_memory > pf_get_free_memory()) return (struct program){0};

    PAGE_DIRECTORY_TYPE * address_space = paging_create_new_address_space();


    disable_wp(); // we copy elf contents based on their permissions, so mapping into non-writable area would page fault
    for (int i = 0; i < ehdr->program_header_entry_count; i++) {
        PH = start + ehdr->program_header_table_offset + i*ehdr->program_header_table_entry_size;
        if (PH->type == ELF_PHT_LOAD) {
            paging_map_to_address_space(address_space, (void*)(unsigned long)PH->vaddr, PH->size_memory, PTE_PDE_PAGE_USER_ACCESS | (PH->flags & ELF_PHF_WRITABLE ? PTE_PDE_PAGE_WRITABLE : 0));
            paging_memset_to_address_space(address_space, (void*)(unsigned long)PH->vaddr, 0, PH->size_memory);
            paging_memcpy_to_address_space(address_space, (void*)(unsigned long)PH->vaddr, start + PH->offset, PH->size_file);
        }
    }
    enable_wp();
    
    paging_map_to_address_space(address_space, PROGRAM_STACK_VADDR-PROGRAM_STACK_SIZE, PROGRAM_STACK_SIZE, PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS);
    paging_map_to_address_space(address_space, PROGRAM_HEAP_VADDR, PROGRAM_HEAP_START_SIZE, PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS);

    return (struct program) {
        .pd_vaddr = address_space,
        .start = (void *)(unsigned long)ehdr->program_entry_offset,
        .stack = PROGRAM_STACK_VADDR,
        .heap = PROGRAM_HEAP_VADDR,
        .kernel_stack = kalloc(PROGRAM_KERNEL_STACK_SIZE)+PROGRAM_KERNEL_STACK_SIZE,
        .kernel_stack_size = PROGRAM_KERNEL_STACK_SIZE,
    };
}

void unload_elf(struct program program) { // don't call when fully loaded, the scheduler dismantles the program
    paging_destroy_address_space(program.pd_vaddr);
}