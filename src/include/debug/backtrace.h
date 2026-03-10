#ifndef BACKTRACE_H
#define BACKTRACE_H

extern struct multiboot_elf_section_header_table * kernel_section_header_table;
struct symbol_lookup {
    const char * symbol;
    void * addr_offset;
};
struct symbol_lookup resolve_symbol(void * addr);
void unwind_stack();
void unwind_stack_vaddr(void * ebp);

#endif