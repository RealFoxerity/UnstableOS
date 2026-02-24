#include <stddef.h>
#include "include/elf.h"
#include "../libc/src/include/string.h"
#include "../libc/src/include/stdio.h"
#include "../libc/src/include/endian.h"
#include "include/fs/fs.h"
#include "include/kernel.h"

char check_elf(int elf_fd) { // returns 1 if elf is not truncated or broken
    ssize_t size = sys_seek(elf_fd, 0, SEEK_END);
    if (size < sizeof(struct elf_header)) return 0;

    sys_seek(elf_fd, 0, SEEK_SET);

    struct elf_header ehdr;
    sys_read(elf_fd, &ehdr, sizeof(struct elf_header));

    if (memcmp((uint8_t *)ehdr.magic, ELF_MAGIC, sizeof(ELF_MAGIC)-1) != 0) return 0;
    if (ehdr.arch != ELF_ARCH_32) return 0; // this tool only supports 32 bit files, TODO: add support to recognise 64 bit files


    if (ehdr.section_header_entry_count != 0) {
        // Section header table outside file bounds
        if (ehdr.section_header_table_offset >= size) return 0;
        // Section header entry index with section names larger than count of sections
        if (ehdr.section_header_table_strings_index >= ehdr.section_header_entry_count) return 0;
        // Section header table truncated
        if (ehdr.section_header_table_offset + ehdr.section_header_table_entry_size*ehdr.section_header_entry_count > size) return 0;

        struct section_header section_names_section; 
        sys_seek(elf_fd, ehdr.section_header_table_offset + ehdr.section_header_table_strings_index*ehdr.section_header_table_entry_size, SEEK_SET);
        sys_read(elf_fd, &section_names_section, sizeof(struct section_header));
        // Section header entry with section names truncated
        if (section_names_section.offset + section_names_section.size > size) return 0;

        struct section_header SH;
        for (int i = 0; i < ehdr.section_header_entry_count; i++) {
            sys_seek(elf_fd, ehdr.section_header_table_offset + ehdr.section_header_table_entry_size * i, SEEK_SET);
            sys_read(elf_fd, &SH, sizeof(struct section_header));

            // Section truncated
            if (SH.offset + SH.size > size) return 0;
            // Invalid section alignment
            if (SH.alignment != 0 && SH.vaddr % SH.alignment != 0) return 0;
        }
    }

    if (ehdr.program_header_entry_count != 0) {
        // Program header table outside file bounds
        if (ehdr.program_header_table_offset >= size) return 0;
        // Program header table truncated
        if (ehdr.program_header_table_offset+ehdr.program_header_entry_count*ehdr.program_header_table_entry_size > size) return 0;

        struct program_header PH;
        for (int i = 0; i < ehdr.program_header_entry_count; i++) {
            sys_seek(elf_fd, ehdr.program_header_table_offset + ehdr.program_header_table_entry_size * i, SEEK_SET);
            sys_read(elf_fd, &PH, sizeof(struct program_header));
            
            // Program section truncated
            if (PH.offset + PH.size_file > size) return 0;
            // Invalid program section size, technically valid but doesn't make any sense
            if (PH.size_file > PH.size_memory) return 0;
            // Invalid program section alignment
            if (PH.alignment != 0 && (PH.vaddr % PH.alignment) != (PH.offset % PH.alignment)) return 0; // i think this is correct? idk
        }
    }
    return 1;
}

/* TODO: rewrite to file descriptors and/or not include at all :P

static const char * section_header_type_names[] = {
    "NULL",
    "PROGBITS",
    "SYMTAB",
    "STRTAB",
    "RELA",
    "HASH",
    "DYNAMIC",
    "NOTE",
    "NOBITS",
    "REL",
    "SHLIB",
    "DYNSYM",
    "INITARR",
    "FINIARR",
    "PINITARR",
    "GROUP",
    "STIDX",
    "NUM",
};

static const char * program_header_type_names[] = {
    "NULL",
    "LOAD",
    "DYNAMIC",
    "INTERP",
    "NOTE",
    "SHLIB",
    "PHDR",
    "TLS",
};

static const char * elf_abi_names[] = {
    [ELF_ABI_SYSTEM_V] = "System V",
    [ELF_ABI_HP_UX] = "HP-UX",
    [ELF_ABI_NETBSD] = "NetBSD",
    [ELF_ABI_LINUX] = "Linux",
    [ELF_ABI_GNU_HURD] = "GNU Hurd",
    [ELF_ABI_SOLARIS] = "Solaris",
    [ELF_ABI_AIX_MONTEREY] = "AIX (Monterey)",
    [ELF_ABI_IRIX] = "IRIX",
    [ELF_ABI_FREEBSD] = "FreeBSD",
    [ELF_ABI_TRU64] = "Tru64",
    [ELF_ABI_NOVELL_MODESTO] = "Novell Modesto",
    [ELF_ABI_OPENBSD] = "OpenBSD",
    [ELF_ABI_OPENVMS] = "OpenVMS",
    [ELF_ABI_NONSTOP_KERNEL] = "NonStop Kernel",
    [ELF_ABI_AROS] = "AROS",
    [ELF_ABI_FENIXOS] = "FenixOS",
    [ELF_ABI_NUXI_CLOUDABI] = "NUXI CloudABI",
    [ELF_ABI_STRATUS_TECH_OPENVOS] = "Stratus Technologies OpenVOS",
};

void readelf(void * start, size_t size) {
    if (!check_elf(start, size)) {
        printf("Exec format error\n");
        return;
    }

    struct elf_header * ehdr = start;

    printf("ELF header:\n\tMagic: \t");
    for (int i = 0; i < offsetof(struct elf_header, object_type); i++) {
        printf("%hhx ", *(char *)(start + i));
    }
    printf("\n\tClass:\t\t\t\t%s\n", ehdr.arch == ELF_ARCH_32?"ELF32":(ehdr.arch == ELF_ARCH_64)?"ELF64":"UNK");
    printf("\tData:\t\t\t\t%s\n", ehdr.data_order == ELF_DO_BE?"Big endian":(ehdr.data_order == ELF_DO_LE)?"Little endian":"UNK");
    printf("\tHeader Version:\t\t\t%d\n", ehdr.elf_header_version);
    printf("\tABI:\t\t\t\t%s\n", ehdr.abi>ELF_ABI_STRATUS_TECH_OPENVOS?"UNK":elf_abi_names[ehdr.abi]==0?"UNK":elf_abi_names[ehdr.abi]);
    printf("\tABI version:\t\t\t%d\n", ehdr.abi_version);
    printf("\tType:\t\t\t\t");
    switch (le16toh(ehdr.object_type)) {
        case ELF_OBJ_REL:
            printf("REL (Relocatable file)");
            break;
        case ELF_OBJ_EXEC:
            printf("EXEC (Executable file)");
            break;
        case ELF_OBJ_DYN:
            printf("DYN (Position-Independant Executable File)");
            break;
        case ELF_OBJ_CORE:
            printf("CORE (Core file)");
            break;

        case ELF_OBJ_NONE:
        default:
            printf("UNK (%hx)", ehdr.object_type);
    }
    printf("\n\tMachine:\t\t\t");
    switch (le16toh(ehdr.arch_isa)) { // there's way too many gaps for this to be usable with string lookup
        case ELF_ISA_UNSPECIFIED:
            printf("No specific ISA");
            break;
        case ELF_ISA_SPARC:
            printf("SPARC");
            break;
        case ELF_ISA_X86:
            printf("x86");
            break;
        case ELF_ISA_MIPS:
            printf("MIPS");
            break;
        case ELF_ISA_POWERPC:
            printf("PowerPC");
            break;
        case ELF_ISA_POWERPC64:
            printf("PowerPC (64-bit)");
            break;
        case ELF_ISA_AARCH32:
            printf("AArch32");
            break;
        case ELF_ISA_SPARCV9:
            printf("SPARCv9");
            break;
        case ELF_ISA_IA64:
            printf("Itanium (64-bit)");
            break;
        case ELF_ISA_X86_64:
            printf("x86-64");
            break;
        case ELF_ISA_AARCH64:
            printf("AArch64");
            break;
        case ELF_ISA_RISCV:
            printf("RISC-V");
            break;
        default:
            printf("UNK");
    }
    printf("\n\tVersion:\t\t\t%d\n", le32toh(ehdr.elf_version));
    printf("\tEntry point address:\t\t");
    if (ehdr.arch == ELF_ARCH_32) printf("0x%x\n", ehdr.program_entry_offset);
    else printf("0x%lx\n", ehdr.program_entry_offset);
    printf("\tStart of program headers:\t%d (bytes into this file)\n", ehdr.arch == ELF_ARCH_32?le32toh(ehdr.program_header_table_offset):le64toh(ehdr.program_header_table_offset));
    printf("\tStart of section headers:\t%d (bytes into this file)\n", ehdr.arch == ELF_ARCH_32?le32toh(ehdr.section_header_table_offset):le64toh(ehdr.section_header_table_offset));
    printf("\tFlags:\t\t\t\t0x%x\n", ehdr.flags);
    printf("\tSize of ELF header:\t\t%d (bytes)\n", le16toh(ehdr.elf_header_size));
    printf("\tSize of a program header:\t%d (bytes)\n", le16toh(ehdr.program_header_table_entry_size));
    printf("\tNumber of program headers:\t%d\n", le16toh(ehdr.program_header_entry_count));
    printf("\tSize of a section header:\t%d (bytes)\n", le16toh(ehdr.section_header_table_entry_size));
    printf("\tNumber of section headers:\t%d\n", le16toh(ehdr.section_header_entry_count));
    printf("\tSection table string index:\t%d\n", le16toh(ehdr.section_header_table_strings_index));

    printf("\nSection Headers:\n");
    if (ehdr.section_header_entry_count == 0) {
        printf("No section headers in file\n");
        goto prog_header;
    }

    struct section_header * section_names_section = start + ehdr.section_header_table_offset + ehdr.section_header_table_strings_index*ehdr.section_header_table_entry_size;

    printf("\t[Nr]\tName\t\tType\t\tAddress\t\tOffset\n");
    printf("\t\tSize\t\tEntSize\t\tFlags Link Info Align\n\n");

    char SHflags[13]; // max possible
    int flag_off = 0;
    
    char * section_names_table = start + section_names_section->offset;
    char * section_name;
    struct section_header * SH = (start + ehdr.section_header_table_offset);
    for (int i = 0; i < ehdr.section_header_entry_count; i++) {
        memset(SHflags, ' ', 13);
        flag_off = 0;

        if (SH->string_offset > section_names_section->size) section_name = NULL;
        else section_name = section_names_table + SH->string_offset;

        printf("\t[%2d]\t%-16s%-8s\t%x\t%x\n", 
            i, 
            section_name ==NULL?"UNK":section_name,
            SH->type > ELF_SHT_NUM?"UNK":section_header_type_names[SH->type],
            SH->vaddr,
            SH->offset);

        if (SH->flags & ELF_SHF_WRITABLE) {
            SHflags[flag_off] = 'W';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_ALLOC) {
            SHflags[flag_off] = 'A';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_EXECINSTR) {
            SHflags[flag_off] = 'X';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_MERGE) {
            SHflags[flag_off] = 'M';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_STRINGS) {
            SHflags[flag_off] = 'S';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_INFO_LINK) {
            SHflags[flag_off] = 'i';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_LINK_ORDER) {
            SHflags[flag_off] = 'L';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_NONCONFORMING) {
            SHflags[flag_off] = 'o';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_GROUP) {
            SHflags[flag_off] = 'G';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_TLS) {
            SHflags[flag_off] = 'T';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_SOLARIS_ORDERED) {
            SHflags[flag_off] = 'O';
            flag_off++;
        }
        if (SH->flags & ELF_SHF_SOLARIS_EXCLUDE) {
            SHflags[flag_off] = 'E';
            flag_off++;
        }
        if (SH->flags & ~(ELF_SHF_SOLARIS_EXCLUDE | ELF_SHF_SOLARIS_ORDERED | ELF_SHF_TLS | ELF_SHF_GROUP | 
                ELF_SHF_NONCONFORMING | ELF_SHF_LINK_ORDER | ELF_SHF_INFO_LINK | ELF_SHF_STRINGS | 
                ELF_SHF_MERGE | ELF_SHF_EXECINSTR | ELF_SHF_ALLOC | ELF_SHF_WRITABLE)) {
                    SHflags[flag_off] = 'x';
                    flag_off++;
        }
        SHflags[flag_off] = '\0';
        printf("\t\t%x\t%x\t%-5s %3d %3d\t%d\n", 
            SH->size,
            SH->entry_size,
            SHflags,
            SH->link,
            SH->info,
            SH->alignment);
        
        SH = (((void*)SH) + ehdr.section_header_table_entry_size);
    }

    printf("Key to Flags:\n"
  "W (write), A (alloc), X (execute), M (merge), S (strings), I (info),\n"
  "L (link order), o (extra OS processing required), G (group), T (TLS),\n"
  "x (unknown), E (exclude), O (ordered link)\n");

    prog_header:
    printf("\nProgram headers:\n");
    if (ehdr.program_header_entry_count == 0) {
        printf("No program headers in file\n");
        goto end;
    }

    printf("\tType\tOffset\t VirtAddr PhysAddr FileSize Mem Size Flags Align\n");


    char PHflags[4];
    PHflags[3] = '\0';
    struct program_header * PH = start + ehdr.program_header_table_offset;
    for (int i = 0; i < ehdr.program_header_entry_count; i++) {
        memset(PHflags, ' ', 3);

        if (PH->flags & ELF_PHF_READABLE) PHflags[0] = 'R';
        if (PH->flags & ELF_PHF_WRITABLE) PHflags[1] = 'W';
        if (PH->flags & ELF_PHF_EXEC) PHflags[2] = 'E';

        printf("\t%s\t%x %x %x %x %x %s   %d\n",
            PH->type > ELF_PHT_TLS?"UNK":program_header_type_names[PH->type],
            PH->offset,
            PH->vaddr,
            PH->resv_paddr,
            PH->size_file,
            PH->size_memory,
            PHflags,
            PH->alignment);

        PH = PH + 1;
    }

    end:
    return;
}
*/