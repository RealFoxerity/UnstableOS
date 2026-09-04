#include "basic.h"
#include "dso.h"
#include "string.h"
#include <UnstableOS/elf.h>

static unsigned long elf_hash(const unsigned char *name) {
    unsigned long hash = 0;
    while (*name) {
        hash = (hash << 4) + *name++;
        hash ^= (hash >> 24) & 0xf0;
    }
    return hash & 0xfffffff;
}

static const struct symbol_table_entry * lookup_in_dso(const char * symbol, const struct dso * dso) {
    unsigned long nbucket = dso->hash_table[0];
    unsigned long nchain = dso->hash_table[1];
    unsigned long bucket = elf_hash((const unsigned char *)symbol) % nbucket;
    unsigned long idx = dso->hash_table[2 + bucket];

    while (1) {
        if (idx == STN_UNDEF)
            return NULL;
        const struct symbol_table_entry * symbol_entry = dso->symtab + idx * dso->symtab_ent_size;
        assert(symbol_entry->string_offset < dso->strtab_size);
        if (!strcmp(dso->strtab + symbol_entry->string_offset, symbol)) {
            if (symbol_entry->section_index != STN_UNDEF)
                return symbol_entry;
            return NULL;
        }
        assert(idx < nchain);
        idx = dso->hash_table[2 + nbucket + idx];
    }
}

struct symbol_result lookup_symbol(const struct symbol_table_entry * ste, const struct dso * start_dso) {
    assert(ste->string_offset < start_dso->strtab_size);
    const char * symbol = start_dso->strtab + ste->string_offset;
    const struct symbol_table_entry * result = NULL;
    result = lookup_in_dso(symbol, start_dso);

    // not 100% sure this is what I'm supposed to do, but hopefully it is
    if (result) {
        if (start_dso->symbolic_resolving ||
            ELF32_ST_VISIBILITY(result->other) == STV_PROTECTED)
                return (struct symbol_result) {.ste = result, .dso = start_dso};
    }

    struct level_list * ll = level_order;
    while (ll) {
        struct dso_list * dso = ll->this;
        while (dso) {
            result = lookup_in_dso(symbol, dso->this);
            if (result) {
                if (ELF32_ST_VISIBILITY(result->other) != STV_HIDDEN &&
                    ELF32_ST_BIND(result->info) != STB_LOCAL)
                    return (struct symbol_result) {.ste = result, .dso = dso->this};
            }
            dso = dso->next;
        }
        ll = ll->next;
    }
    if (ELF32_ST_BIND(ste->info) == STB_WEAK)
        return (struct symbol_result){0};

    dbg_print("rtld: Error: Failed to find the address for `");
    dbg_print(symbol);
    dbg_print("` required by `");
    dbg_print(start_dso->name);
    dbg_print("`\n");
    abort();
}