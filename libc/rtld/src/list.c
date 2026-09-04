#include "dso.h"
#include "basic.h"
#include <stddef.h>
#include "string.h"

// very slow and naive implementations, however,
// there's realistically never gonna be more than 64 libraries
// and the kernel limits us to 1000 anyways, so linear searches aren't that bad here

struct dso * dso_list = NULL;
struct level_list * level_order = NULL;

struct dso * dso_was_loaded(const char * name) {
    struct dso * dso = dso_list;
    while (dso) {
        if (dso->name && strcmp(name, dso->name) == 0) {
            dso->more_instances = 1;
            return dso;
        }
        dso = dso->next;
    }
    return NULL;
}

void append_dso(struct dso * dso) {
    dso->next = NULL;
    if (dso_list == NULL)
        dso_list = dso;
    else
        dso_list->prev->next = dso;

    dso->prev = dso_list->prev;
    dso_list->prev = dso;
}



// We need to sort the DSOs by their depth in the dependency graph
// this is needed for the symbol resolving logic System V ABI wants
// however there could be the same DSO multiple times present in the graph,
//  or even linked weirdly around
// point is, we can't allow to process dependency chains when those dependencies
//  are already in the list higher up
// primarily to save space, but this also solves circular dependency issues

static char library_exists_higher(struct level_list * level, struct dso * dso) {
    struct level_list * curr = level_order;
    do {
        struct dso_list * dl = curr->this;
        while (dl) {
            if (dl->this == dso)
                return 1;
            dl = dl->next;
        }
        curr = curr->next;
    } while (curr && curr != level);

    return 0;
}

static void _level_order_dsos(struct level_list * last_level, struct dso * last_dso) {
    if (!last_level->next) {
        last_level->next = alloc(sizeof(struct level_list));
        memset(last_level->next, 0, sizeof(struct level_list));
    }
    struct level_list * level = last_level->next;
    struct dso_list * range_end = level->this;

    // first pass to gather all dependencies to make library_exists_higher work
    for (struct dso_list * dso = last_dso->deps; dso; dso = dso->next) {
        if (dso->this->more_instances && library_exists_higher(level, dso->this))
            continue;
        struct dso_list * new = alloc(sizeof(struct dso_list));
        new->this = dso->this;
        new->next = level->this;

        level->this = new;
    }

    // sort the new DSOs
    struct dso_list * dso = level->this;
    while (dso && dso != range_end) {
        _level_order_dsos(level, dso->this);
        dso = dso->next;
    }
}
void level_order_dsos() {
    level_order = alloc(sizeof(struct level_list));
    level_order->this = alloc(sizeof(struct dso_list));
    level_order->this->this = main_elf;
    level_order->this->next = NULL;
    level_order->next = NULL;
    _level_order_dsos(level_order, main_elf);
}

static void relink_at_start(struct dso * dso) {
    // unlink
    if (dso->next)
        dso->next->prev = dso->prev;
    else
        dso_list->prev = dso->prev;
    if (dso != dso_list)
        dso->prev->next = dso->next;
    else
        dso_list = dso->next;

    // relink at start
    dso->prev = dso_list->prev;
    dso_list->prev = dso;
    dso->next = dso_list;
    dso_list = dso;
}

static char is_dependent(const struct dso * dso) {
    struct dso_list * dep = dso->deps;
    while (dep) {
        if (!dep->this->sorted)
            return 1;
        dep = dep->next;
    }
    return 0;
}

void sort_dsos() {
    struct dso * unsorted_dso_head = NULL;

    char found = 0;
    char sorted = 0;
    char force_sort = 0;
    do {
        found = 0;
        sorted = 1;
        struct dso * dso = unsorted_dso_head ? unsorted_dso_head : dso_list;
        while (dso) {
            struct dso * next = dso->next;
            if (!is_dependent(dso) || force_sort) {
                found = 1;
                if (unsorted_dso_head == NULL || unsorted_dso_head == dso)
                    unsorted_dso_head = next;
                dso->sorted = 1;
                relink_at_start(dso);

                force_sort = 0;
            } else sorted = 0;
            dso = next;
        }
        if (!found && !sorted) {
            dbg_print("rtld: Warning: Cyclic dependency detected, will try to resolve\n");
            force_sort = 1;
        }
    } while (!sorted);
}