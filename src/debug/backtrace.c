#include "../include/kernel.h"
#include "../include/mm/kernel_memory.h"
#include "../include/debug/backtrace.h"

#include "../include/elf.h"

struct multiboot_elf_section_header_table * kernel_section_header_table = NULL;

static struct section_header strtab = {0};
static struct section_header shstrtab = {0};
static struct section_header symtab = {0};

static char init_symbol_table() {
    if (kernel_section_header_table == NULL || kernel_section_header_table->addr == 0) {
        kprintf("No kernel symbols available!\n");
        return 0;
    }

    const struct section_header * sh_table =
        (void *)(unsigned long)kernel_section_header_table->addr;

    shstrtab = sh_table[kernel_section_header_table->shndx];

    for (int i = 0; i < kernel_section_header_table->num; i++) {
        if (sh_table[i].type == ELF_SHT_SYMTAB) {
            if (symtab.size < sh_table[i].size) {
                symtab = sh_table[i];
            }
        } else if (sh_table[i].type == ELF_SHT_STRTAB) {
            if (strtab.size < sh_table[i].size) {
                strtab = sh_table[i];
            }
        }

    }

    if (symtab.vaddr == 0 || symtab.size == 0) {
        kprintf("No .symtab section found!");
        shstrtab = strtab = symtab = (struct section_header){0};
        return 0;
    }
    if (strtab.vaddr == 0 || strtab.size == 0) {
        kprintf("No .strtab section found!");
        shstrtab = strtab = symtab = (struct section_header){0};
        return 0;
    }

    return 1;
}

#define SYMBOL_FAILED_LOOKUP "????????"
struct symbol_lookup resolve_symbol(void * addr) {
    struct symbol_lookup out = {0};
    if (symtab.vaddr == 0) {
        not_found:
        out.symbol = SYMBOL_FAILED_LOOKUP;
        out.addr_offset = addr;
        return out;
    }

    struct symbol_table_entry * entries = (void*)(unsigned long)symtab.vaddr;
    if (entries == NULL) goto not_found;

    struct symbol_table_entry * closest_entry = NULL;

    for (int i = 0; i < symtab.size / symtab.entry_size; i++) {
        // can't be <= because it's the *next* instruction pointer
        // consequently, >= is possible
        if (entries[i].value < (unsigned long)addr &&
            entries[i].value + entries[i].size >= (unsigned long)addr)
        {
            if (closest_entry == NULL ||
                closest_entry->value < entries[i].value)
            {
                closest_entry = &entries[i];
            }
        }
    }
    if (closest_entry == NULL) goto not_found;

    out.symbol = (const char*)(unsigned long)strtab.vaddr + closest_entry->string_offset;
    out.addr_offset = (void*)addr - closest_entry->value;
    return out;
}

struct stack_frame {
    struct stack_frame * next_frame;
    void * eip;
};


void unwind_stack_vaddr(void * ebp) {
    if (symtab.vaddr == 0) init_symbol_table();

    struct stack_frame * frame = ebp;

    kprintf("backtrace:\n");
    size_t depth = 0;
    for (depth = 0;
        paging_get_pte(frame) != NULL &&
        paging_get_pte(frame+1) != NULL &&
        (void*)frame > (void*)LOWEST_PHYS_ADDR_ALLOWABLE; depth ++)
    {
        if (frame->eip == 0) break;
        struct symbol_lookup symbol = resolve_symbol(frame->eip);
        kprintf("%10lu %s+%p [%p]\n", depth, symbol.symbol, symbol.addr_offset, frame->eip);
        frame = frame->next_frame;
    }
    if (paging_get_pte(frame) == NULL || paging_get_pte(frame+1) == NULL || frame->eip != NULL) kprintf("%10lu "SYMBOL_FAILED_LOOKUP"+"SYMBOL_FAILED_LOOKUP" ["SYMBOL_FAILED_LOOKUP"]\n", depth);
}

void unwind_stack() {
    unwind_stack_vaddr(__builtin_frame_address(0));
}