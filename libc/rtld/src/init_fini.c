#include "dso.h"
#include "basic.h"

void call_init() {
    if (main_elf->preinit_array) {
        for (int i = 0; i < main_elf->preinit_arraysz / sizeof(void (*)()); i++) {
            if (main_elf->preinit_array[i])
                main_elf->preinit_array[i]();
        }
    }

    struct dso * dso = dso_list;
    do {
        dso = dso->prev;

        if (dso->init)
            dso->init();
        for (int i = 0; i < dso->init_arraysz / sizeof(void (*)()); i++) {
            if (dso->init_array[i])
                dso->init_array[i]();
        }
    } while (dso != dso_list);
}

void call_fini() {
    struct dso * dso = dso_list;
    while (dso) {
        if (dso->fini)
            dso->fini();
        for (int i = 0; i < dso->fini_arraysz / sizeof(void (*)()); i++) {
            if (dso->fini_array[i])
                dso->fini_array[i]();
        }

        dso = dso->next;
    }
}