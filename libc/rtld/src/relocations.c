#include "basic.h"
#include "string.h"
#include "dso.h"
#include <UnstableOS/elf.h>

static void itoa(unsigned int num, char * out) {
    int ctr = 0;
    for (; ; ctr++) {
        out[ctr] = '0' + num % 10;
        num /= 10;
        if (num == 0) break;
    }

    for (int i = 0; i <= ctr/2; i++) {
        char temp = out[i];
        out[i] = out[ctr-i];
        out[ctr-i] = temp;
    }
    out[ctr+1] = '\0';
}

// commented out relocations are ones for linking, not dynamic linking, and thus irrelevant
static void do_rela(const struct rela_entry * rela, const struct dso * dso) {
    struct symbol_table_entry dummy = {
        .value = 0,
        .size = 0,
        .string_offset = 0,
        .other = 0,
        .section_index = STN_UNDEF,
        .info = 0,
    };
    struct symbol_result sr = {
        .dso = dso,
        .ste = &dummy,
    };
    void * symbol = 0;
    void ** target = rela->offset + dso->load_base;
    *target = NULL;

    if (ELF32_R_SYM(rela->info) != STN_UNDEF) {
        struct symbol_table_entry * ste = dso->symtab + ELF32_R_SYM(rela->info) * dso->symtab_ent_size;
        sr = lookup_symbol(ste, dso);
        if (!sr.ste && ELF32_ST_BIND(ste->info) == STB_WEAK)
            return;
        assert(sr.ste);
        assert(sr.dso);
        symbol = sr.ste->value + sr.dso->load_base;
        assert(symbol);
    };

    switch (ELF32_R_TYPE(rela->info)) {
        case R_386_NONE:
            break;
        case R_386_32:
            *target = symbol + rela->addend;
            break;
        case R_386_PC32:
            *target = symbol + rela->addend - (unsigned long)target;
            break;
        //case R_386_GOT32:
        //case R_386_PLT32:
        case R_386_COPY:
            break;
        case R_386_GLOB_DAT:
            *target = symbol;
            break;
        case R_386_JUMP_SLOT:
            *target = symbol;
            break;
        case R_386_RELATIVE:
            *target = dso->load_base + rela->addend;
            break;
        case R_386_GOTOFF:
            *target = symbol + rela->addend - (unsigned long)dso->got;
            break;
        case R_386_GOTPC:
            *target = dso->got + rela->addend - (unsigned long)target;
            break;
        case R_386_16:
            *(uint16_t**)target = symbol + rela->addend;
            break;
        case R_386_PC16:
            *(uint16_t**)target = symbol + rela->addend - (unsigned long)target;
            break;
        case R_386_8:
            *(uint8_t**)target = symbol + rela->addend;
            break;
        case R_386_PC8:
            *(uint8_t**)target = symbol + rela->addend - (unsigned long)target;
            break;
        case R_386_SIZE32:
            *(unsigned long *)target = sr.ste->size + rela->addend;
            break;
        case R_386_IRELATIVE:
            assert_msg(0, "R_386_IRELATIVE not yet supported\n");

        case R_386_TLS_TPOFF:
            *(long*)target = -(long)(sr.dso->tls_start + sr.ste->value + rela->addend);
            break;
        //case R_386_TLS_IE:
        //case R_386_TLS_GOTIE:
        //case R_386_TLS_LE:
        //case R_386_TLS_GD:
        //case R_386_TLS_LDM:
        //case R_386_TLS_GD_32:
        //case R_386_TLS_GD_PUSH:
        //case R_386_TLS_GD_CALL:
        //case R_386_TLS_GD_POP:
        //case R_386_TLS_LDM_32:
        //case R_386_TLS_LDM_PUSH:
        //case R_386_TLS_LDM_CALL:
        //case R_386_TLS_LDM_POP:
        //case R_386_TLS_LDO_32:
        //case R_386_TLS_IE_32:
        //case R_386_TLS_LE_32:
        case R_386_TLS_DTPMOD32:
            *(unsigned long*)target = dso->dvtid;
            break;
        case R_386_TLS_DTPOFF32:
            *(unsigned long*)target = sr.ste->value + rela->addend;
            break;
        case R_386_TLS_TPOFF32:
            *(long*)target = -(long)(sr.dso->tls_start + sr.ste->value + rela->addend);
            break;
        //case R_386_TLS_GOTDESC:
        //case R_386_TLS_DESC_CALL:
        case R_386_TLS_DESC:
            assert_msg(0, "R_386_TLS_DESC not yet supported\n");

        default:
            dbg_print("rtld: Error: Unsupported relocation: ");
            char num[16];
            itoa(ELF32_R_TYPE(rela->info), num);
            dbg_print(num);
            dbg_print("\n");
            abort();
    }

}

static void _process_rel(const struct dso * dso, struct rel_section rels) {
    if (!rels.reltable_start)
        return;
    for (int i = 0; i < rels.reltable_size/rels.entry_size; i++) {
        struct rela_entry * rel = rels.reltable_start + rels.entry_size * i;
        struct rela_entry rela = *rel;
        rela.addend = *(int32_t*)(dso->load_base + rel->offset);
        do_rela(&rela, dso);
    }
}

static void _process_rela(const struct dso * dso, struct rel_section relas) {
    if (!relas.reltable_start)
        return;
    for (int i = 0; i < relas.reltable_size/relas.entry_size; i++) {
        struct rela_entry * rela = relas.reltable_start + relas.entry_size * i;
        do_rela(rela, dso);
    }
}

static void _relocate_dso(struct dso * dso) {
    _process_rel(dso, dso->rel);
    _process_rela(dso, dso->rela);

    if (dso->pltrel_type == DT_RELA) {
        dso->pltrel.entry_size = dso->rela.entry_size ? dso->rela.entry_size : sizeof(struct rela_entry);
        _process_rela(dso, dso->pltrel);
    } else {
        dso->pltrel.entry_size = dso->rel.entry_size ? dso->rel.entry_size : sizeof(struct rel_entry);
        _process_rel(dso, dso->pltrel);
    }
}


void relocate_dsos() {
    struct dso * dso = dso_list;
    while (dso) {
        _relocate_dso(dso);
        dso = dso->next;
    }
}