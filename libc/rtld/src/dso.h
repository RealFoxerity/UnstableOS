#ifndef DSO_H
#define DSO_H

struct rel_section {
    void * reltable_start;
    unsigned long reltable_size;
    unsigned long entry_size; // rel/rela size for pltrel
};

#include <UnstableOS/elf.h>
struct dso_list;
// all pointers are already adjusted against load_base
struct dso {
    const char * name;
    void * load_base;

    struct dynamic_entry * dynamic;
    unsigned long dynamic_elements;
    unsigned char dt_flags;
    unsigned char symbolic_resolving;
    unsigned char pltrel_type; // DT_REL/DT_RELA

    const char * strtab;
    unsigned long strtab_size;

    void * symtab; // i'd do struct symbol_table_entry, but symtab_ent_size might be a weird number
    unsigned long symtab_ent_size;
    unsigned long * hash_table;

    struct rel_section rel;
    struct rel_section rela;
    struct rel_section pltrel; // defined by DT_JMPREL and DT_PLTRELSZ, always R_386_JUMP_SLOT

    unsigned long dvtid;
    unsigned long tls_start;

    void * got;

    void (*_start)(); // only needed for the main executable
    void (*init)();
    void (*fini)();
    void (**preinit_array)();
    void (**init_array)();
    void (**fini_array)();
    unsigned long preinit_arraysz;
    unsigned long init_arraysz;
    unsigned long fini_arraysz;

    const char * runpath;

    struct dso_list * deps;

    struct dso *prev, *next; // doubly linked list of all DSOs to use for topology sort

    // for tsort whether this specific DSO has already been sorted
    //   and shouldn't be counted towards the indegree count of a depending object
    char sorted;
    // for level order to optimize the dedup
    char more_instances;
};
// has to be done to keep a full dependency tree
struct dso_list {
    struct dso * this;
    struct dso_list * next;
};
struct level_list {
    struct dso_list * this;
    struct level_list * next;
};
extern struct dso * main_elf;
extern struct dso * dso_list;
extern struct level_list * level_order;

struct dso * loadelf(int fd);

struct dso * dso_was_loaded(const char * name);
void append_dso(struct dso * dso);
void level_order_dsos();
void relocate_dsos();
void sort_dsos();

struct symbol_result {
    const struct symbol_table_entry * ste;
    const struct dso * dso;
};

struct symbol_result lookup_symbol(const struct symbol_table_entry * ste, const struct dso * start_dso);

void call_init();
void call_fini();
#endif