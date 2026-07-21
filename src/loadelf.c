#include <stddef.h>
#include "elf.h"
#include "fs/fs.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_sched.h"
#include <string.h>

#define kprintf(fmt, ...) kprintf("ELF loader: "fmt, ##__VA_ARGS__)

// TODO: Extremely prone to race conditions on files, do exclusive open on the fd in the future

struct program load_elf(int elf_fd) { // returns VIRTUAL address of page directory for the elf's new address space
    ssize_t elf_size = sys_seek(elf_fd, 0, SEEK_END);
    if (elf_size < 0) return (struct program){0};
    // assumes kernel's address space (aka. data available in all address spaces)
    // assumes program and thread stacks are above heap, and that the program cannot load anything between the heap and stacks

    if (!check_elf(elf_fd)) return (struct program){0};

    struct elf_header ehdr;
    sys_seek(elf_fd, 0, SEEK_SET);
    sys_read(elf_fd, &ehdr, sizeof(struct elf_header));

    // architecture dependant things that shouldn't be in check_elf
    if (ehdr.elf_header_version != ELF_HEADER_VERSION || ehdr.elf_version != ELF_VERSION) return (struct program){0};
    if (ehdr.arch_isa != ELF_ISA_X86 || ehdr.arch != ELF_ARCH_32) return (struct program){0};

    char found_loadable = 0;
    size_t needed_memory = PROGRAM_STACK_START_SIZE + PROGRAM_KERNEL_STACK_SIZE +
                            PROGRAM_DVT_SIZE + __PROGRAM_TCB_SIZE;
    size_t static_tls_size = 0;
    size_t tls_program_headers = 0;
    struct program_header PH;
    for (int i = 0; i < ehdr.program_header_entry_count; i++) {
        sys_seek(elf_fd, ehdr.program_header_table_offset + i*ehdr.program_header_table_entry_size, SEEK_SET);
        sys_read(elf_fd, &PH, sizeof(struct program_header));

        if (PH.type == ELF_PHT_LOAD) {
            found_loadable = 1;
            if ((void *)(unsigned long)PH.vaddr < kernel_mem_top) return (struct program){0}; // obv can't load into the kernel and the program probably wouldn't run without a loadable segment
            if ((void*)(unsigned long)PH.vaddr + PH.size_memory > PROGRAM_HEAP_VADDR) return (struct program){0}; // same but stacks and heap
            needed_memory += PH.size_memory;
        } else if (PH.type == ELF_PHT_TLS) {
            static_tls_size += PH.size_memory;
            tls_program_headers ++;
        }
    }
    needed_memory += static_tls_size;

    if (!found_loadable)
        return (struct program){0}; // useless trying to load an ELF file without any loadable segments
    if (needed_memory > pf_get_free_memory())
        return (struct program){0};
    if (static_tls_size > PROGRAM_MAX_TLS_SIZE - sizeof(struct thread_control_block))
        return (struct program){0};
    if (tls_program_headers > PROGRAM_MAX_TLS_ENTRIES)
        return (struct program){0};

    PAGE_DIRECTORY_TYPE * address_space = paging_create_new_address_space();

    #define ELF_COPY_BUFFER_SIZE 1024
    unsigned char * copy_buffer = kalloc(ELF_COPY_BUFFER_SIZE);
    kassert(copy_buffer);

    unsigned long * dvt = kalloc(PROGRAM_DVT_SIZE);
    kassert(dvt);

    memset(dvt, 0, PROGRAM_DVT_SIZE); // is generation id 0 okay?

    paging_map_to_address_space(address_space,
        PROGRAM_DVT_VADDR, PROGRAM_DVT_SIZE,
        PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE);

    paging_map_to_address_space(address_space,
        PROGRAM_TLS_BLUEPRINT_VADDR,
        PROGRAM_MAX_TLS_SIZE,PTE_PDE_PAGE_USER_ACCESS);

    paging_map_to_address_space(address_space,
        PROGRAM_PCB_VADDR, PAGE_SIZE_NO_PAE,
        PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE);
    paging_memset_to_address_space(address_space,
        PROGRAM_PCB_VADDR, 0,
        PAGE_SIZE_NO_PAE);

    disable_wp(); // we copy elf contents based on their permissions, so mapping into non-writable area would page fault
    paging_memset_to_address_space(address_space,
        PROGRAM_TLS_BLUEPRINT_VADDR,
        0, PROGRAM_MAX_TLS_SIZE);

    size_t tls_block_idx = 0;
    unsigned long last_tls_block_offset = 0;
    for (int i = 0; i < ehdr.program_header_entry_count; i++) {
        sys_seek(elf_fd, ehdr.program_header_table_offset + i*ehdr.program_header_table_entry_size, SEEK_SET);
        sys_read(elf_fd, &PH, sizeof(struct program_header));
        if (PH.type == ELF_PHT_LOAD) {
            paging_map_to_address_space(address_space, (void*)(unsigned long)PH.vaddr, PH.size_memory, PTE_PDE_PAGE_USER_ACCESS | (PH.flags & ELF_PHF_WRITABLE ? PTE_PDE_PAGE_WRITABLE : 0));
            paging_memset_to_address_space(address_space, (void*)(unsigned long)PH.vaddr, 0, PH.size_memory);

            sys_seek(elf_fd, PH.offset, SEEK_SET);
            for (int j = 0; j < PH.size_file / ELF_COPY_BUFFER_SIZE; j++) {
                sys_read(elf_fd, copy_buffer, ELF_COPY_BUFFER_SIZE);
                paging_memcpy_to_address_space(address_space, (void*)(unsigned long)PH.vaddr + j*ELF_COPY_BUFFER_SIZE, copy_buffer, ELF_COPY_BUFFER_SIZE);
            }
            if (PH.size_file % ELF_COPY_BUFFER_SIZE != 0) {
                sys_read(elf_fd, copy_buffer, PH.size_file % ELF_COPY_BUFFER_SIZE);
                paging_memcpy_to_address_space(address_space, (void*)(unsigned long)PH.vaddr + PH.size_file - (PH.size_file % ELF_COPY_BUFFER_SIZE), copy_buffer, PH.size_file % ELF_COPY_BUFFER_SIZE);
            }
        } else if (PH.type == ELF_PHT_TLS) {
            tls_block_idx ++; // intentional, idx 0 is generation id
            last_tls_block_offset += PH.size_memory;
            dvt[tls_block_idx] = last_tls_block_offset;

            void * daddr = PROGRAM_TLS_BLUEPRINT_TOP_VADDR - last_tls_block_offset;

            sys_seek(elf_fd, PH.offset, SEEK_SET);
            for (int j = 0; j < PH.size_file / ELF_COPY_BUFFER_SIZE; j++) {
                sys_read(elf_fd, copy_buffer, ELF_COPY_BUFFER_SIZE);
                paging_memcpy_to_address_space(address_space, daddr + j*ELF_COPY_BUFFER_SIZE, copy_buffer, ELF_COPY_BUFFER_SIZE);
            }
            if (PH.size_file % ELF_COPY_BUFFER_SIZE != 0) {
                sys_read(elf_fd, copy_buffer, PH.size_file % ELF_COPY_BUFFER_SIZE);
                paging_memcpy_to_address_space(address_space, daddr + PH.size_file - (PH.size_file % ELF_COPY_BUFFER_SIZE), copy_buffer, PH.size_file % ELF_COPY_BUFFER_SIZE);
            }
        }
    }
    paging_memcpy_to_address_space(address_space,
        PROGRAM_DVT_VADDR,
        dvt,
        PROGRAM_DVT_SIZE);

    enable_wp();
    kfree(dvt);
    kfree(copy_buffer);

    return (struct program) {
        .pd_vaddr = address_space,
        .start = (void *)(unsigned long)ehdr.program_entry_offset,
        .stack = PROGRAM_STACK_VADDR,
        .heap = PROGRAM_HEAP_VADDR,
    };
}

void unload_elf(struct program program) { // don't call when fully loaded, the scheduler dismantles the program
    paging_destroy_address_space(program.pd_vaddr);
}