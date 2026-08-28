#include <stddef.h>
#include "elf.h"
#include "fs/fs.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_sched.h"
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

#define kprintf(fmt, ...) kprintf("ELF Reloc: "fmt, ##__VA_ARGS__)

char load_elf_relocate(file_descriptor_t * file,
    struct vm_record * vmr, void * address_space,
    struct rela_entry relocation, uintptr_t rela_offset,
    off_t symtab, size_t syment,
    char read_addend)
{

    struct vm_record * closest =
            (struct vm_record *)rbtree_search_lte(
                (rbtree_t*)vmr, rela_offset + relocation.offset);

    if (!closest || closest->node.val + closest->len < rela_offset + relocation.offset) {
        kprintf("Warning: Relocation outside of loadable segments, skipping\n");
        return 0;
    }

    // manually map the page with this relocation target
    // there isn't anything better we can do...
    // even if we were doing MAP_PRIVATE page caches,
    // this would immediately duplicate the page anyway
    if (!paging_virt_addr_to_phys((void*)rela_offset + relocation.offset)) {
        if (!paging_add_page((void*)rela_offset + relocation.offset,
            PTE_PDE_PAGE_USER_ACCESS |
            (closest->prot & PROT_WRITE ? PTE_PDE_PAGE_WRITABLE : 0))
        ) {
            kprintf("OOM\n");
            return 1;
        }
        uintptr_t page = (rela_offset + relocation.offset) & ~(PAGE_SIZE - 1);
        off_t target_offset = page - closest->node.val + closest->mapping_offset;
        size_t read_len = closest->len - (page - closest->node.val);
        if (read_len > PAGE_SIZE)
            read_len = PAGE_SIZE;
        if (pread_file(file, (void*)page, read_len, target_offset) != read_len)
            return 1;
    }

    if (read_addend)
        relocation.addend = *(int32_t*)(rela_offset + relocation.offset);

    struct symbol_table_entry ste = {0};
    if (ELF32_R_SYM(relocation.info) != ELF_STN_UNDEF) {
        if (pread_file(file,
            &ste, sizeof(struct symbol_table_entry),
            symtab + ELF32_R_SYM(relocation.info) * syment) != sizeof(struct symbol_table_entry))
                return 1;
    }

    uintptr_t symbol_value = ste.value;
    uint32_t final_value = 0;
    switch (ELF32_R_TYPE(relocation.info)) {
        case ELF_RELT_386_NONE:
            break;
        case ELF_RELT_386_32:
            final_value = rela_offset + symbol_value + relocation.addend;
            break;
        case ELF_RELT_386_PC32:
            // the rela_offset here cancels out
            final_value = symbol_value + relocation.addend - relocation.offset;
            break;
        //case ELF_RELT_386_GOT32:
        //case ELF_RELT_386_PLT32:
        case ELF_RELT_386_COPY:
            break; // huh?
        case ELF_RELT_386_GLOB_DAT:
        case ELF_RELT_386_JMP_SLOT:
            final_value = rela_offset + symbol_value;
            break;
        case ELF_RELT_386_RELATIVE:
            final_value = rela_offset + relocation.addend;
            break;
        //case ELF_RELT_386_GOTOFF:
        //case ELF_RELT_386_GOTPC:
            //break;
        default:
            kprintf("Unsupported reltype %hhu, missing interpreter?\n", ELF32_R_TYPE(relocation.info));
            return 0;
    }
    *(uint32_t*)(rela_offset + relocation.offset) = final_value;
    return 0;
}