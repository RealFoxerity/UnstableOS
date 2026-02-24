#ifndef ELF_H
#define ELF_H
#include <stdint.h>
#include <stddef.h>

#define ELF_MAGIC "\x7F\x45\x4C\x46" // 0x7F ELF
#define ELF_ARCH_32 1
#define ELF_ARCH_64 2

#define ELF_DO_LE 1
#define ELF_DO_BE 2

#define ELF_VERSION 1 
#define ELF_HEADER_VERSION ELF_VERSION

enum elf_abi {
    ELF_ABI_SYSTEM_V,
    ELF_ABI_HP_UX,
    ELF_ABI_NETBSD,
    ELF_ABI_LINUX,
    ELF_ABI_GNU_HURD,
    ELF_ABI_SOLARIS = 0x06,
    ELF_ABI_AIX_MONTEREY,
    ELF_ABI_IRIX,
    ELF_ABI_FREEBSD,
    ELF_ABI_TRU64,
    ELF_ABI_NOVELL_MODESTO,
    ELF_ABI_OPENBSD,
    ELF_ABI_OPENVMS,
    ELF_ABI_NONSTOP_KERNEL,
    ELF_ABI_AROS,
    ELF_ABI_FENIXOS,
    ELF_ABI_NUXI_CLOUDABI,
    ELF_ABI_STRATUS_TECH_OPENVOS,
};

enum elf_object_types {
    ELF_OBJ_NONE,
    ELF_OBJ_REL,
    ELF_OBJ_EXEC,
    ELF_OBJ_DYN,
    ELF_OBJ_CORE,
    /*FE00 - FFFF reserved*/
};

enum elf_isa { // pnly the common ones
    ELF_ISA_UNSPECIFIED = 0,
    ELF_ISA_SPARC = 2,
    ELF_ISA_X86 = 0x03,
    ELF_ISA_MIPS = 0x08,
    ELF_ISA_POWERPC = 0x14,
    ELF_ISA_POWERPC64,
    ELF_ISA_AARCH32 = 0x28, // up to armv7/aarch32
    ELF_ISA_SPARCV9 = 0x2B,
    ELF_ISA_IA64 = 0x32, // itanium
    ELF_ISA_X86_64 = 0x3E,
    ELF_ISA_AARCH64 = 0xB7,
    ELF_ISA_RISCV = 0xF3
};
struct elf_header {
    uint8_t magic[4];
    uint8_t arch;
    uint8_t data_order;
    uint8_t elf_header_version;
    uint8_t abi;
    uint8_t abi_version; // doesn't matter but usually 0 for System V ABI
    uint8_t __pad[7];
    uint16_t object_type;
    uint16_t arch_isa;
    uint32_t elf_version;
    uint32_t program_entry_offset;
    uint32_t program_header_table_offset;
    uint32_t section_header_table_offset;
    uint32_t flags; // no flags for x86 are defined
    uint16_t elf_header_size;
    uint16_t program_header_table_entry_size;
    uint16_t program_header_entry_count;
    uint16_t section_header_table_entry_size;
    uint16_t section_header_entry_count;
    uint16_t section_header_table_strings_index; // index of the section header table entry that contains section names
} __attribute__((packed));


struct elf_header_64 {
    uint8_t magic[4];
    uint8_t arch;
    uint8_t data_order;
    uint8_t elf_header_version;
    uint8_t abi;
    uint8_t abi_version;
    uint8_t __pad[7];
    uint16_t object_type;
    uint16_t arch_isa;
    uint32_t elf_version;
    uint64_t program_entry_offset;
    uint64_t program_header_table_offset;
    uint64_t section_header_table_offset;
    uint32_t flags;
    uint16_t elf_header_size;
    uint16_t program_header_table_entry_size;
    uint16_t program_header_entry_count;
    uint16_t section_header_table_entry_size;
    uint16_t section_header_entry_count;
    uint16_t section_header_table_strings_index;
} __attribute__((packed));

enum program_header_types {
    ELF_PHT_NULL,
    ELF_PHT_LOAD, // loadable segment
    ELF_PHT_DYNAMIC, // dynamic linking information
    ELF_PHT_INTERP, // interpreter information
    ELF_PHT_NOTE,
    ELF_PHT_SHLIB, // reserved
    ELF_PHT_PHDR, // segment containing program header table
    ELF_PHT_TLS, // thread-local storage template
    /*
        0x60000000 - 6FFFFFFF reserved OS specific
        0x70000000 - 7FFFFFFF reserved CPU specific
    */
};

enum program_header_flags {
    ELF_PHF_EXEC = 1,
    ELF_PHF_WRITABLE,
    ELF_PHF_READABLE = 4,
};

struct program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t resv_paddr; // no clue what this is
    uint32_t size_file;
    uint32_t size_memory;
    uint32_t flags;
    uint32_t alignment; // 0/1 = no alignment, power of 2 requiring data to be loaded aligned
} __attribute__((packed));

struct program_header_64 {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t resv_paddr;
    uint64_t size_file;
    uint64_t size_memory;
    uint64_t alignment;
} __attribute__((packed));


enum section_header_types {
    ELF_SHT_NULL, 
    ELF_SHT_PROGBITS, // Program data
    ELF_SHT_SYMTAB, // Symbol table
    ELF_SHT_STRTAB, // String table
    ELF_SHT_RELA, // Relocation entries with addends
    ELF_SHT_HASH, // Symbol hash table
    ELF_SHT_DYNAMIC, // Dynamic linking information
    ELF_SHT_NOTE,
    ELF_SHT_NOBITS, // Program space with no data (bss)
    ELF_SHT_REL, // Relocation entries, no addends
    ELF_SHT_SHLIB, // Reserved
    ELF_SHT_DYNSYM, // Dynamic linker symbol table
    ELF_SHT_INIT_ARRAY, // Array of constructors
    ELF_SHT_FINI_ARRAY, // Array of destructors
    ELF_SHT_PREINIT_ARRAY, // Array of pre-constructors
    ELF_SHT_GROUP, // Section group
    ELF_SHT_SYMTAB_SHNDX, // Extended section indices
    ELF_SHT_NUM, // Number of defined types
    // 0x60000000 - reserved OS specific
};
enum section_header_flags {
    ELF_SHF_WRITABLE = 1,
    ELF_SHF_ALLOC, // Occupies memory in execution
    ELF_SHF_EXECINSTR = 4, // executable
    ELF_SHF_MERGE = 0x10, // might be merged ????
    ELF_SHF_STRINGS = 0x20, // null-terminated strings
    ELF_SHF_INFO_LINK = 0x40, // SHT index in info
    ELF_SHF_LINK_ORDER = 0x80, // preserve order after combining
    ELF_SHF_NONCONFORMING = 0x100, // Non-standard OS specific handling required 
    ELF_SHF_GROUP = 0x200, // section is a part of a group
    ELF_SHF_TLS = 0x400, // sections holds thread-local data
    // 0x0FF00000 OS specific mask
    // 0xF0000000 CPU specific mask
    ELF_SHF_SOLARIS_ORDERED = 0x4000000, // special ordering requirement
    ELF_SHF_SOLARIS_EXCLUDE = 0x8000000, // section excluded unless used
};

struct section_header {
    uint32_t string_offset; // offset into .shstrtab (offset into section names)
    uint32_t type;
    uint32_t flags;
    uint32_t vaddr;
    uint32_t offset; // into file
    uint32_t size;
    uint32_t link; // Contains the section index of an associated section.
    uint32_t info; // Contains extra information about the section.
    uint32_t alignment;
    uint32_t entry_size; // if section contains a table, the size of each entry
};

struct section_header_64 {
    uint32_t string_offset;
    uint32_t type;
    uint64_t flags;
    uint64_t vaddr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
};

void readelf(void * start, size_t size);


#include "mm/kernel_memory.h"
#include "kernel_sched.h"

struct program load_elf(int elf_fd);
void unload_elf(struct program program);
char check_elf(int elf_fd); // returns 1 if elf is not truncated or broken, generic test, not supported test
#endif