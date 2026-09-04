#include <stddef.h>
#include "elf.h"
#include "fs/fs.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_sched.h"
#include <string.h>
#include <stdlib.h>

#define kprintf(fmt, ...) kprintf("ELF loader: "fmt, ##__VA_ARGS__)

#define ELF_ASLR_START 0x9000000
#define ELF_INTERP_ASLR_START 0x78000000
#define ELF_ASLR_PAGES 4096

#define ELF_INTERP_MAX_PATH 512 // to not allocate 4K buffers, it's going to be /usr/lib/rtld.so anyway

#include <sys/mman.h>

#include <errno.h>
#include "mm/mmap.h"

char load_elf_relocate(file_descriptor_t * file,
    struct vm_record * vmr, void * address_space,
    struct rela_entry relocation, uintptr_t rela_offset,
    off_t symtab, size_t syment,
    char read_addend);

ssize_t exec_safe_argv_dup(char * const* argv, char * const* envp, void * stack_top_addr, int elf_fd, char ** stack_out, auxv_t ** execfd_auxv_out);


struct program load_elf(int * status, const char * path, char * const* argv, char * const* envp, int elf_fd) { // returns VIRTUAL address of page directory for the elf's new address space
    kassert(status);
    // the exec_safe_argv_dup isn't thread safe (primarily because of strcpy)
    // this is a bandaid fix for it
    // shouldn't be that bad performance-wise considering we only copy at max 16K
    // though I assume we skip at least 2 ticks like this
    // TODO: FIX!
    char * stack_state = NULL;
    auxv_t * execfd = NULL;
    spinlock_acquire(&scheduler_lock);
    ssize_t stack_state_sz = exec_safe_argv_dup(argv, envp, PROGRAM_STACK_VADDR, elf_fd, &stack_state, &execfd);
    spinlock_release(&scheduler_lock);

    if (stack_state_sz < 0) {
        *status = stack_state_sz;
        return (struct program){0};
    }

    file_descriptor_t * main_elf = NULL;
    int file_status = openat_file((void*)AT_FDCWD, path, O_RDONLY, 0, &main_elf, 0);
    if (file_status < 0) {
        *status = file_status;
        kfree(stack_state);
        return (struct program){0};
    }
    if (!S_ISREG(main_elf->inode->mode)) {
        close_file(main_elf);
        kfree(stack_state);
        *status = -EACCES;
        return (struct program){0};
    }

    // assumes kernel's address space (aka. data available in all address spaces)
    // assumes program and thread stacks are above heap, and that the program cannot load anything between the heap and stacks

    file_descriptor_t * file = main_elf;
    char parsing_interp = 0;
    restart_checks:

    if (parsing_interp) {
        file_status = openat_file((void*)AT_FDCWD, path, O_RDONLY, 0, &file, 1);
        kfree((void*)path);
        if (file_status < 0) {
            kfree(stack_state);
            close_file(main_elf);
            *status = file_status;
            return (struct program){0};
        }
        if (!S_ISREG(file->inode->mode)) {
            close_file(main_elf);
            close_file(file);
            kfree(stack_state);
            *status = -EACCES;
            return (struct program){0};
        }
    }

    if (!check_elf(file)) goto early_err;

    struct elf_header ehdr;
    if (pread_file(file, &ehdr, sizeof(struct elf_header), 0) != sizeof(struct elf_header))
        goto early_err;

    if (ehdr.object_type != ELF_OBJ_EXEC && ehdr.object_type != ELF_OBJ_DYN)
        goto early_err;

    uintptr_t rela_offset = ehdr.object_type == ELF_OBJ_DYN
        ? (parsing_interp ? ELF_INTERP_ASLR_START : ELF_ASLR_START) + ((rand() % ELF_ASLR_PAGES) << 12)
        : 0;
    //kprintf("Loading at relative offset %lx\n", rela_offset);
    // architecture dependant things that shouldn't be in check_elf
    if (ehdr.elf_header_version != ELF_HEADER_VERSION || ehdr.elf_version != ELF_VERSION) goto early_err;
    if (ehdr.arch_isa != ELF_ISA_X86 || ehdr.arch != ELF_ARCH_32) goto early_err;

    char found_loadable = 0;
    size_t needed_memory = PROGRAM_STACK_START_SIZE + PROGRAM_KERNEL_STACK_SIZE +
                            PROGRAM_DVT_SIZE + __PROGRAM_TCB_SIZE;
    size_t static_tls_size = 0;
    size_t tls_program_headers = 0;
    struct program_header PH;
    for (int i = 0; i < ehdr.program_header_entry_count; i++) {
        if (pread_file(file, &PH, sizeof(struct program_header),
            ehdr.program_header_table_offset + i*ehdr.program_header_table_entry_size) !=
                sizeof(struct program_header))
            goto early_err;

        if (PH.type == PT_INTERP) {
            if (parsing_interp) {
                kprintf("Nested ELF interpreters are not supported!\n");
                goto early_err;
            }
            if (PH.size_file >= ELF_INTERP_MAX_PATH) {
                kprintf("ELF interpreter path exceeds %d characters!\n", ELF_INTERP_MAX_PATH);
                goto early_err;
            }
            char * path2 = kalloc(PH.size_file + 1);
            if (!path2) goto early_err;

            if (pread_file(file, path2, PH.size_file, PH.offset) != PH.size_file) {
                kfree(path2);
                goto early_err;
            }
            path2[PH.size_file] = '\0';
            //kprintf("loading interp %s\n", path2);
            path = path2;
            parsing_interp = 1;
            goto restart_checks;
        }
        if (PH.type == PT_LOAD) {
            found_loadable = 1;
            if ((void *)PH.vaddr + rela_offset < kernel_mem_top) goto early_err; // obv can't load into the kernel and the program probably wouldn't run without a loadable segment
            if ((void*)PH.vaddr + PH.size_memory + rela_offset > (void*)PROGRAM_PCB_VADDR) goto early_err; // same but stacks and heap
            needed_memory += PH.size_memory;
        } else if (PH.type == PT_TLS) {
            static_tls_size += PH.size_memory;
            tls_program_headers ++;
        }
    }
    needed_memory += static_tls_size;

    if (!found_loadable)
        goto early_err; // useless trying to load an ELF file without any loadable segments
    if (needed_memory > pf_get_free_memory())
        goto early_err;
    if (static_tls_size > PROGRAM_MAX_TLS_SIZE)
        goto early_err;
    if (tls_program_headers > PROGRAM_MAX_TLS_ENTRIES)
        goto early_err;

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

    // due to the way TLS loading works, the interpreter has to have write access to the TLS blueprint range
    // normal PIE don't need the write access, so we can mark is as unwritable
    paging_map_to_address_space(address_space,
        PROGRAM_TLS_BLUEPRINT_VADDR,
        PROGRAM_MAX_TLS_SIZE,PTE_PDE_PAGE_USER_ACCESS | (parsing_interp ? PTE_PDE_PAGE_WRITABLE : 0));

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

    // once we start doing relocations, the cost of mapping the page tables becomes too high
    // better to just switch address spaces
    void * old_address_space = paging_get_address_space_paddr();

    for (int i = 0; i < ehdr.program_header_entry_count; i++) {
        if (pread_file(file, &PH, sizeof(struct program_header),
            ehdr.program_header_table_offset + i*ehdr.program_header_table_entry_size) !=
                sizeof(struct program_header))
                    goto err;
        if (PH.type == PT_LOAD) {
            if (PH.vaddr % PAGE_SIZE) {
                PH.size_file += PH.vaddr % PAGE_SIZE;
                PH.size_memory += PH.vaddr % PAGE_SIZE;
                PH.offset -= PH.vaddr % PAGE_SIZE;
                PH.vaddr &= ~(PAGE_SIZE - 1);
            }
            munmap_to_vmr(&vmr, (void*)PH.vaddr + rela_offset, PH.size_memory, 1, 1);
            void * res = mmap_to_vmr(&vmr,
                (void*)PH.vaddr + rela_offset, PH.size_file,
                (PH.flags & ELF_PHF_WRITABLE ? PROT_WRITE : 0) | PROT_READ, MAP_PRIVATE | MAP_FIXED,
                file, PH.offset);
            if (res > (void*)-100) {
                goto err;
            }
            if (PH.size_memory > PH.size_file && PH.size_memory - PH.size_file > PAGE_SIZE) {
                PH.size_file += PAGE_SIZE - 1;
                PH.size_file &= ~(PAGE_SIZE - 1);
                PH.vaddr += PH.size_file;

                res = mmap_to_vmr(&vmr,
                (void*)PH.vaddr + rela_offset, PH.size_memory - PH.size_file,
                (PH.flags & ELF_PHF_WRITABLE ? PROT_WRITE : 0) | PROT_READ,
                MAP_PRIVATE | MAP_FIXED | MAP_ANON,
                0, 0);
                if (res > (void*)-100) {
                    goto err;
                }
            }
        } else if (PH.type == PT_TLS) {
            // we can't use mmap on TLS because of how our kernel thread create works
            tls_block_idx ++; // intentional, idx 0 is generation id
            last_tls_block_offset += PH.size_memory;
            dvt[tls_block_idx] = last_tls_block_offset;

            void * daddr = PROGRAM_TLS_BLUEPRINT_TOP_VADDR - last_tls_block_offset;

            disable_wp();
            seek_file(file, PH.offset, SEEK_SET);
            for (int j = 0; j < PH.size_file / ELF_COPY_BUFFER_SIZE; j++) {
                if (read_file(file, copy_buffer, ELF_COPY_BUFFER_SIZE) != ELF_COPY_BUFFER_SIZE)
                    goto err;
                paging_memcpy_to_address_space(address_space, daddr + j*ELF_COPY_BUFFER_SIZE, copy_buffer, ELF_COPY_BUFFER_SIZE);
            }
            if (PH.size_file % ELF_COPY_BUFFER_SIZE != 0) {
                if (read_file(file, copy_buffer, PH.size_file % ELF_COPY_BUFFER_SIZE) != PH.size_file % ELF_COPY_BUFFER_SIZE)
                    goto err;
                paging_memcpy_to_address_space(address_space, daddr + PH.size_file - (PH.size_file % ELF_COPY_BUFFER_SIZE), copy_buffer, PH.size_file % ELF_COPY_BUFFER_SIZE);
            }
            enable_wp();
        }
    }
    paging_memcpy_to_address_space(address_space,
        PROGRAM_DVT_VADDR,
        dvt,
        PROGRAM_DVT_SIZE);

    // second pass for relocations
    PH.type = PT_NULL;
    for (int i = 0; i < ehdr.program_header_entry_count; i++) {
        if (pread_file(file, &PH, sizeof(struct program_header),
            ehdr.program_header_table_offset + i*ehdr.program_header_table_entry_size) !=
                sizeof(struct program_header))
                    goto err;

        if (PH.type == PT_DYNAMIC)
            break;
    }

    off_t rel_start = 0, rela_start = 0;
    size_t rel_ent = 0, rela_ent = 0;
    size_t rel_sz = 0, rela_sz = 0;

    off_t symtab = 0;
    size_t syment = 0;
    if (PH.type == PT_DYNAMIC) {
        struct rela_entry re = {0};
        struct dynamic_entry de;
        for (size_t i = 0; i < PH.size_file / sizeof(struct dynamic_entry); i++) {
            if (pread_file(file, &de, sizeof(struct dynamic_entry),
                PH.offset + i*sizeof(struct dynamic_entry)) != sizeof(struct dynamic_entry))
                    goto err;

            //if (de.type > ELF_DT_LOOS)
            //    continue;
            // TODO: "An object file may have multiple relocation sections."
            // https://www.sco.com/developers/gabi/latest/ch5.dynamic.html
            switch (de.type) {
                case DT_NULL:
                    goto done;

                // RELA shouldn't occur at all on IA-32
                case DT_RELA:
                    rela_start = de.val;
                    break;
                case DT_RELASZ:
                    rela_sz = de.val;
                    break;
                case DT_RELAENT:
                    rela_ent = de.val;
                    break;

                // addend in the offset location
                case DT_REL:
                    rel_start = de.val;
                    break;
                case DT_RELSZ:
                    rel_sz = de.val;
                    break;
                case DT_RELENT:
                    rel_ent = de.val;
                    break;
                case DT_SYMTAB:
                    symtab = de.val;
                    break;
                case DT_SYMENT:
                    syment = de.val;
                    break;
                default:
                    //kprintf("Unsupported dtype %lu, missing interpreter?\n", de.type);
                    break;
            }
        }

        done:
        paging_apply_address_space(paging_virt_addr_to_phys(address_space));
        disable_wp();
        if ((rel_start || rela_start) && (!symtab || !syment))
            kprintf("Warning: Missing symbol table info, relocations might be wrong\n");
        if (rel_start) {
            for (size_t i = 0; i < rel_sz/rel_ent; i++) {
                if (pread_file(file,
                    &re, sizeof(struct rel_entry),
                    rel_start + i * rel_ent) != sizeof(struct rel_entry))
                        goto reloc_err;

                if (load_elf_relocate(file, vmr, address_space, re, rela_offset, symtab, syment, 1))
                    goto reloc_err;
            }
        }
        if (rela_start) {
            for (size_t i = 0; i < rela_sz/rela_ent; i++) {
                if (pread_file(file,
                    &re, sizeof(struct rela_entry),
                    rela_start + i * rela_ent) != sizeof(struct rela_entry))
                    goto reloc_err;

                if (load_elf_relocate(file, vmr, address_space, re, rela_offset, symtab, syment, 0))
                    goto reloc_err;
            }
        }
        enable_wp();
        paging_apply_address_space(old_address_space);
    }

    kfree(dvt);
    kfree(copy_buffer);

    // also handles closing of main_elf if !parsing_interp
    close_file(file);

    if (!parsing_interp)
        execfd->type = AT_NULL;

    return (struct program) {
        .main_executable = parsing_interp ? main_elf : NULL,
        .pd_vaddr = address_space,
        .start = (void *)ehdr.program_entry_offset + rela_offset,
        .vm = vmr,
        .stack_image = stack_state,
        .stack_size = stack_state_sz
    };


    reloc_err:
    enable_wp();

    err:
    paging_apply_address_space(old_address_space);
    munmap_free_vm(vmr, 1);
    paging_destroy_address_space(address_space);

    early_err:
    kfree(stack_state);
    close_file(main_elf);
    if (file != main_elf)
        close_file(file);
    *status = -ENOEXEC;
    return (struct program){0};
}
