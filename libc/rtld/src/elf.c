#include "basic.h"
#include "files.h"
#include "dso.h"
#include "string.h"
#include "tls.h"
#include "mmap.h"
#include <time.h>
#include <UnstableOS/syscalls.h>
#include <UnstableOS/elf.h>

static unsigned int * dvt = PROGRAM_DVT_VADDR;
static void * tls_blueprint = PROGRAM_TLS_BLUEPRINT_TOP_VADDR;

static uint32_t ___internal_rand_state = 1;
#define RAND_MAX 32768
static int rand() {
    if (__builtin_expect(___internal_rand_state == 0, 0)) {
        struct timespec time;
        ___internal_rand_state = syscall(SYSCALL_CLOCK_GETTIME, CLOCK_MONOTONIC, &time);
        ___internal_rand_state = time.tv_nsec >> 16;
        ___internal_rand_state ^= time.tv_sec;
    }

    // https://en.wikipedia.org/wiki/Xorshift

    ___internal_rand_state ^= ___internal_rand_state << 13;
    ___internal_rand_state ^= ___internal_rand_state >> 17;
    ___internal_rand_state ^= ___internal_rand_state << 5;

    return (int)(___internal_rand_state % RAND_MAX);
}

#define ASLR_PAGES 256
struct dso * loadelf(int fd) {
    static void * last_page = (void *) 0x08000000;
    static unsigned int dvt_idx = 1;
    static unsigned int tls_offset = 0;

    last_page += (rand() % ASLR_PAGES) * PAGESIZE;
    void * end_page = last_page;

    assert(fd >= 0);
    struct dso * dso = alloc(sizeof(struct dso));
    memset(dso, 0, sizeof(struct dso));
    assert(dso);
    dso->load_base = last_page;

    struct elf_header ehdr;

    assert(pread(fd, &ehdr, sizeof(struct elf_header), 0) == sizeof(struct elf_header));
    assert(memcmp(&ehdr.magic, ELF_MAGIC, sizeof(ehdr.magic)) == 0);

    // obviously the dynamic linker won't be loading position dependant files
    assert(ehdr.object_type        == ELF_OBJ_DYN);

    assert(ehdr.elf_header_version == ELF_HEADER_VERSION && ehdr.elf_version == ELF_VERSION);
    assert(ehdr.arch_isa           == ELF_ISA_X86        && ehdr.arch        == ELF_ARCH_32);

    dso->_start = last_page + ehdr.program_entry_offset;

    struct program_header PH;

    for (int i = 0; i < ehdr.program_header_entry_count; i++) {
        assert(pread(fd, &PH, sizeof(struct program_header),
            ehdr.program_header_table_offset + i*ehdr.program_header_table_entry_size) ==
                sizeof(struct program_header));

        switch (PH.type) {
            case PT_LOAD:
                if (PH.vaddr % PAGE_SIZE) {
                    PH.size_file += PH.vaddr % PAGE_SIZE;
                    PH.size_memory += PH.vaddr % PAGE_SIZE;
                    PH.offset -= PH.vaddr % PAGE_SIZE;
                    PH.vaddr &= ~(PAGE_SIZE - 1);
                }
                void * target = last_page + PH.vaddr;
                // MAP_FIXED automatically unmaps conflicting ranges
                assert(mmap(
                    target, PH.size_memory,
                    (PH.flags & ELF_PHF_WRITABLE ? PROT_WRITE : 0) | PROT_READ,
                    MAP_PRIVATE | MAP_FIXED | MAP_ANON,
                    -1, 0) == target);
                assert(mmap(
                    target, PH.size_file,
                    (PH.flags & ELF_PHF_WRITABLE ? PROT_WRITE : 0) | PROT_READ,
                    MAP_PRIVATE | MAP_FIXED,
                    fd, PH.offset) == target);
                if (target + PH.size_memory > end_page)
                    end_page = target + PH.size_memory;
                break;

            case PT_TLS:
                assert(dvt_idx + 1 <= MAX_DTV_ENTRIES);
                assert(tls_offset + PH.size_memory <= PROGRAM_MAX_TLS_SIZE);
                tls_offset += PH.size_memory;
                assert(pread(fd, tls_blueprint - tls_offset, PH.size_file, PH.offset) == PH.size_file);
                dso->dvtid = dvt_idx;
                dso->tls_start = tls_offset;

                dvt[dvt_idx++] = tls_offset;
                dvt[0] = dvt_idx; // generation number, whatever that means

                if (target + PH.size_memory > end_page)
                    end_page = target + PH.size_memory;
                break;
            case PT_DYNAMIC:
                dso->dynamic = last_page + PH.vaddr;
                dso->dynamic_elements = PH.size_file / sizeof(struct dynamic_entry);
                break;
            case PT_INTERP:
                if (fd == exec_fd)
                    break;
                dbg_print("rtld: Shared objects requiring interpreters are not supported\n");
                abort();
            default:
                break;
        }
    }

    if (dso->dynamic == NULL)
        goto dynamic_end;

    // Gather all needed dynamic info for later processing
    for (int i = 0; i < dso->dynamic_elements; i++) {
        switch (dso->dynamic[i].type) {
            case DT_NULL:
                goto dynamic_end_1;
            case DT_PLTRELSZ:
                assert_msg(!dso->pltrel.reltable_size, "Duplicate DT_PLTRELSZ entries!");
                dso->pltrel.reltable_size = dso->dynamic[i].val;
                break;
            case DT_PLTGOT: // i386 only has GOT here
                assert_msg(!dso->got, "Duplicate DT_PLTGOT entries!");
                dso->got = last_page + dso->dynamic[i].ptr;
                break;
            case DT_HASH:
                assert_msg(!dso->hash_table, "Duplicate DT_HASH entries!");
                dso->hash_table = last_page + dso->dynamic[i].ptr;
                break;
            case DT_STRTAB:
                assert_msg(!dso->strtab, "Duplicate DT_STRTAB entries!");
                dso->strtab = last_page + dso->dynamic[i].ptr;
                break;
            case DT_SYMTAB:
                assert_msg(!dso->symtab, "Duplicate DT_SYMTAB entries!");
                dso->symtab = last_page + dso->dynamic[i].ptr;
                break;
            case DT_RELA:
                assert_msg(!dso->rela.reltable_start, "Duplicate DT_RELA entries!");
                dbg_print("rtld: Warning: RELA on i386 is not ABI conformant\n");
                dso->rela.reltable_start = last_page + dso->dynamic[i].ptr;
                break;
            case DT_RELASZ:
                assert_msg(!dso->rela.reltable_size, "Duplicate DT_RELASZ entries!");
                //dbg_print("rtld: Warning: RELASZ on i386 is not ABI conformant\n");
                dso->rela.reltable_size = dso->dynamic[i].val;
                break;
            case DT_RELAENT:
                assert_msg(!dso->rela.entry_size, "Duplicate DT_RELAENT entries!");
                //dbg_print("rtld: Warning: RELAENT on i386 is not ABI conformant\n");
                dso->rela.entry_size = dso->dynamic[i].val;
                break;
            case DT_STRSZ:
                assert_msg(!dso->strtab_size, "Duplicate DT_STRSZ entries!");
                dso->strtab_size = dso->dynamic[i].val;
                break;
            case DT_SYMENT:
                assert_msg(!dso->symtab_ent_size, "Duplicate DT_SYMENT entries!");
                dso->symtab_ent_size = dso->dynamic[i].val;
                break;
            case DT_INIT:
                assert_msg(!dso->init, "Duplicate DT_INIT entries!");
                if (!dso->dynamic[i].ptr)
                    break;
                dso->init = last_page + dso->dynamic[i].ptr;
                break;
            case DT_FINI:
                assert_msg(!dso->fini, "Duplicate DT_FINI entries!");
                if (!dso->dynamic[i].ptr)
                    break;
                dso->fini = last_page + dso->dynamic[i].ptr;
                break;
            case DT_SYMBOLIC:
                dso->symbolic_resolving = 1;
                break;
            case DT_REL:
                assert_msg(!dso->rel.reltable_start, "Duplicate DT_REL entries!");
                dso->rel.reltable_start = last_page + dso->dynamic[i].ptr;
                break;
            case DT_RELSZ:
                assert_msg(!dso->rel.reltable_size, "Duplicate DT_RELSZ entries!");
                dso->rel.reltable_size = dso->dynamic[i].val;
                break;
            case DT_RELENT:
                assert_msg(!dso->rel.entry_size, "Duplicate DT_RELENT entries!");
                dso->rel.entry_size = dso->dynamic[i].val;
                break;
            case DT_PLTREL:
                assert_msg(!dso->pltrel_type, "Duplicate DT_PLTREL entries!");
                dso->pltrel_type = dso->dynamic[i].val;
                switch (dso->dynamic[i].val) {
                    case DT_RELA:
                        dbg_print("rtld: Warning: RELA as PLTREL on i386 is not ABI conformant\n");
                    case DT_REL:
                        break;
                    default:
                        dbg_print("rltd: Error: Unknown procedure linkage table relocation type!\n");
                        abort();
                }
                break;
            case DT_JMPREL:
                assert_msg(!dso->pltrel.reltable_start, "Duplicate DT_JMPREL entries!");
                dso->pltrel.reltable_start = last_page + dso->dynamic[i].ptr;
                break;
            case DT_INIT_ARRAY:
                assert_msg(!dso->init_array, "Duplicate DT_INIT_ARRAY entries!");
                dso->init_array = last_page + dso->dynamic[i].ptr;
                break;
            case DT_FINI_ARRAY:
                assert_msg(!dso->fini_array, "Duplicate DT_FINI_ARRAY entries!");
                dso->fini_array = last_page + dso->dynamic[i].ptr;
                break;
            case DT_INIT_ARRAYSZ:
                assert_msg(!dso->init_arraysz, "Duplicate DT_INIT_ARRAYSZ entries!");
                dso->init_arraysz = dso->dynamic[i].val;
                break;
            case DT_FINI_ARRAYSZ:
                assert_msg(!dso->fini_arraysz, "Duplicate DT_FINI_ARRAYSZ entries!");
                dso->fini_arraysz = dso->dynamic[i].val;
                break;
            case DT_FLAGS:
                dso->dt_flags = dso->dynamic[i].val;
                break;
            case DT_PREINIT_ARRAY:
                assert_msg(!dso->preinit_array, "Duplicate DT_PREINIT_ARRAY entries!");
                if (fd != exec_fd)
                    break;
                dso->preinit_array = last_page + dso->dynamic[i].ptr;
                break;
            case DT_PREINIT_ARRAYSZ:
                assert_msg(!dso->preinit_arraysz, "Duplicate DT_PREINIT_ARRAYSZ entries!");
                if (fd != exec_fd)
                    break;
                dso->preinit_arraysz = dso->dynamic[i].val;
                break;

            case DT_RPATH:
            case DT_RUNPATH:
                dso->runpath = (const char *)dso->dynamic[i].ptr;
                break;

            case DT_TEXTREL:
                dbg_print("rtld: Unimplemented: RELRO (DT_TEXTREL) is not currently supported!\n");
                abort();

            case DT_NEEDED: // will be handled in the next stage
            case DT_SONAME:
            case DT_DEBUG:
            case DT_BIND_NOW:
            case DT_SYMTAB_SHNDX:
            default:
                break;
        }
    }
    dynamic_end_1:
    if ((!dso->rel.reltable_start || !dso->rel.reltable_size || !dso->rel.entry_size) &&
        !(dso->rel.reltable_start == NULL && dso->rel.reltable_size == 0 && dso->rel.entry_size == 0)) {
        dbg_print("rtld: Error: Object missing parts of needed REL info!\n");
        abort();
    }
    if ((!dso->rela.reltable_start || !dso->rela.reltable_size || !dso->rela.reltable_size) &&
        !(dso->rela.reltable_start == NULL && dso->rela.reltable_size == 0 && dso->rela.entry_size == 0)) {
        dbg_print("rtld: Error: Object missing parts of needed RELA info!\n");
        abort();
    }
    assert_msg(dso->hash_table,        "Object is missing DT_HASH!");
    assert_msg(dso->strtab,            "Object is missing DT_STRTAB!");
    assert_msg(dso->strtab_size,       "Object is missing DT_STRSZ!");
    assert_msg(dso->symtab,            "Object is missing DT_SYMTAB!");
    assert_msg(dso->symtab_ent_size,   "Object is missing DT_SYMENT!");

    if (dso->pltrel_type && !dso->got) {
        dbg_print("rtld: Error: Object requiring PLT/GOT relocations is missing DT_PLTGOT!\n");
        abort();
    }
    if (dso->pltrel_type && !dso->pltrel.reltable_start) {
        dbg_print("rtld: Error: Object requiring PLT/GOT relocations is missing DT_JMPREL!\n");
        abort();
    }
    if (dso->pltrel_type && !dso->pltrel.reltable_size) {
        dbg_print("rtld: Error: Object requiring PLT/GOT relocations is missing DT_PLTRELSZ!\n");
        abort();
    }
    if (dso->preinit_array && !dso->preinit_arraysz) {
        dbg_print("rtld: Error: Object requiring .preinit_array is missing DT_PREINIT_ARRAYSZ!\n");
        abort();
    }
    if (dso->init_array && !dso->init_arraysz) {
        dbg_print("rtld: Error: Object requiring .init_array is missing DT_INIT_ARRAYSZ!\n");
        abort();
    }
    if (dso->fini_array && !dso->fini_arraysz) {
        dbg_print("rtld: Error: Object requiring .fini_array is missing DT_FINI_ARRAYSZ!\n");
        abort();
    }

    if (dso->dt_flags & DF_SYMBOLIC)
        dso->symbolic_resolving = 1;
    if (dso->dt_flags & DF_ORIGIN) {
        dbg_print("rtld: Unimplemented: Path substitution (DF_ORIGIN) is not currently supported!\n");
        abort();
    }
    if (dso->dt_flags & DF_TEXTREL) {
        dbg_print("rtld: Unimplemented: RELRO (DF_TEXTREL) is not currently supported!\n");
        abort();
    }

    if (dso->runpath)
        dso->runpath += (unsigned long)dso->strtab;

    dynamic_end:

    last_page = end_page + PAGE_SIZE - 1;
    last_page = (void*)((unsigned long)end_page & ~(PAGE_SIZE - 1));

    last_page += PAGESIZE; // inter-object gap
    //dbg_print("loaded\n");
    append_dso(dso);

    if (!dso->dynamic)
        return dso;

    for (int i = 0; i < dso->dynamic_elements; i++) {
        if (dso->dynamic[i].type == DT_NULL)
            break;
        if (dso->dynamic[i].type != DT_NEEDED)
            continue;

        const char * soname = dso->strtab + dso->dynamic[i].val;
        //dbg_print("rtld: Loading dependency `");
        //dbg_print(soname);
        //dbg_print("`... ");

        struct dso_list * deplist = alloc(sizeof(struct dso_list));
        assert(deplist);
        memset(deplist, 0, sizeof(struct dso_list));

        struct dso * so = dso_was_loaded(soname);
        if (so) {
            //dbg_print("found\n");
            goto link;
        }

        int sofd = openelf(soname,dso->runpath);
        assert(sofd >= 0);
        so = loadelf(sofd);
        so->name = soname;
        close(sofd);

        link:
        deplist->next = dso->deps;
        deplist->this = so;
        dso->deps = deplist;
    }

    return dso;
}