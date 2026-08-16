#include <stddef.h>
#include "elf.h"
#include "fs/fs.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_sched.h"
#include <string.h>

#define kprintf(fmt, ...) kprintf("ELF loader: "fmt, ##__VA_ARGS__)

#include <sys/mman.h>
#include "mm/mmap.h"
struct program load_elf(file_descriptor_t * file) { // returns VIRTUAL address of page directory for the elf's new address space
    if (!file || !file->inode)
        return (struct program){0};
    // assumes kernel's address space (aka. data available in all address spaces)
    // assumes program and thread stacks are above heap, and that the program cannot load anything between the heap and stacks

    if (!check_elf(file)) return (struct program){0};

    struct elf_header ehdr;
    pread_file(file, &ehdr, sizeof(struct elf_header), 0);

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
        pread_file(file, &PH, sizeof(struct program_header),
            ehdr.program_header_table_offset + i*ehdr.program_header_table_entry_size);

        if (PH.type == ELF_PHT_LOAD) {
            found_loadable = 1;
            if ((void *)PH.vaddr < kernel_mem_top) return (struct program){0}; // obv can't load into the kernel and the program probably wouldn't run without a loadable segment
            if ((void*)PH.vaddr + PH.size_memory > PROGRAM_HEAP_VADDR) return (struct program){0}; // same but stacks and heap
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

    disable_wp();
    paging_memset_to_address_space(address_space,
        PROGRAM_TLS_BLUEPRINT_VADDR,
        0, PROGRAM_MAX_TLS_SIZE);
    enable_wp();

    size_t tls_block_idx = 0;
    unsigned long last_tls_block_offset = 0;
    struct vm_record * vmr = NULL;

    for (int i = 0; i < ehdr.program_header_entry_count; i++) {
        pread_file(file, &PH, sizeof(struct program_header),
            ehdr.program_header_table_offset + i*ehdr.program_header_table_entry_size);
        if (PH.type == ELF_PHT_LOAD) {
            if (PH.vaddr % PAGE_SIZE) {
                PH.size_file += PH.vaddr % PAGE_SIZE;
                PH.offset -= PH.vaddr % PAGE_SIZE;
                PH.vaddr &= ~(PAGE_SIZE - 1);
            }
            munmap_to_vmr(&vmr, (void*)PH.vaddr, PH.size_memory, 1, 1);
            void * res = mmap_to_vmr(&vmr,
                (void*)PH.vaddr, PH.size_file,
                (PH.flags & ELF_PHF_WRITABLE ? PROT_WRITE : 0) | PROT_READ, MAP_PRIVATE | MAP_FIXED,
                file, PH.offset);
            if (res > (void*)-100) {
                munmap_free_vm(vmr, 1);
                return (struct program){0};
            }
            if (PH.size_memory > PH.size_file && PH.size_memory - PH.size_file > PAGE_SIZE) {
                PH.size_file += PAGE_SIZE - 1;
                PH.size_file &= ~(PAGE_SIZE - 1);
                PH.vaddr += PH.size_file;

                res = mmap_to_vmr(&vmr,
                (void*)PH.vaddr, PH.size_memory - PH.size_file,
                (PH.flags & ELF_PHF_WRITABLE ? PROT_WRITE : 0) | PROT_READ,
                MAP_PRIVATE | MAP_FIXED | MAP_ANON,
                0, 0);
                if (res > (void*)-100) {
                    munmap_free_vm(vmr, 1);
                    return (struct program){0};
                }
            }
        } else if (PH.type == ELF_PHT_TLS) {
            // we can't use mmap on TLS because of how our kernel thread create works
            tls_block_idx ++; // intentional, idx 0 is generation id
            last_tls_block_offset += PH.size_memory;
            dvt[tls_block_idx] = last_tls_block_offset;

            void * daddr = PROGRAM_TLS_BLUEPRINT_TOP_VADDR - last_tls_block_offset;

            disable_wp();
            seek_file(file, PH.offset, SEEK_SET);
            for (int j = 0; j < PH.size_file / ELF_COPY_BUFFER_SIZE; j++) {
                read_file(file, copy_buffer, ELF_COPY_BUFFER_SIZE);
                paging_memcpy_to_address_space(address_space, daddr + j*ELF_COPY_BUFFER_SIZE, copy_buffer, ELF_COPY_BUFFER_SIZE);
            }
            if (PH.size_file % ELF_COPY_BUFFER_SIZE != 0) {
                read_file(file, copy_buffer, PH.size_file % ELF_COPY_BUFFER_SIZE);
                paging_memcpy_to_address_space(address_space, daddr + PH.size_file - (PH.size_file % ELF_COPY_BUFFER_SIZE), copy_buffer, PH.size_file % ELF_COPY_BUFFER_SIZE);
            }
            enable_wp();
        }
    }
    paging_memcpy_to_address_space(address_space,
        PROGRAM_DVT_VADDR,
        dvt,
        PROGRAM_DVT_SIZE);

    kfree(dvt);
    kfree(copy_buffer);

    return (struct program) {
        .pd_vaddr = address_space,
        .start = (void *)ehdr.program_entry_offset,
        .vm = vmr,
        .stack = PROGRAM_STACK_VADDR,
        .heap = PROGRAM_HEAP_VADDR,
    };
}
