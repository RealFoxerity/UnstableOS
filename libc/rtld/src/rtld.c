#include <stddef.h>
#include <UnstableOS/elf.h>
#include <UnstableOS/syscalls.h>
#include "basic.h"
#include "dso.h"
#include "string.h"
#include "files.h"
#include "tls.h"

char ** environ = NULL;

void init_heap();

auxv_t * auxv = NULL;

const char * ld_path = NULL;

struct dso * main_elf = NULL;
int exec_fd = -1;
int tracing = 0;
__attribute__((noreturn)) void abort() {
    syscall(SYSCALL_ABORT);
    __builtin_unreachable();
}
__attribute__((noreturn)) void exit(int status) {
    syscall(SYSCALL_EXIT, status);
    __builtin_unreachable();
}

static char * getenv(const char * name) {
    for (int i = 0; environ[i] != NULL; i++) {
        char * equals = strchrnul(environ[i], '=');
        if (strncmp(name, environ[i], equals - environ[i]) == 0) {
            return equals + 1;
        }
    }
    return NULL;
}

// returns the target _start
void (*__rtld_main(int argc, char ** argv))() {
    environ = argv + argc + 1;

    for (int i = 0; ; i++) {
        if (environ[i] == NULL) {
            auxv = (auxv_t *)&environ[i+1];
            break;
        }
    }

    if ((!auxv || auxv[0].type == AT_NULL) && argc < 2) {
        dbg_print("rtld: missing executable\n");
        abort();
    }

    if (auxv) {
        for (auxv_t * ax = auxv; ax->type != AT_NULL; ax ++) {
            if (ax->type == AT_EXECFD) {
                exec_fd = ax->un.val;
                break;
            }
        }
        if (exec_fd == -1 && argc < 2) {
            dbg_print("rtld: called with no AT_EXECFD!\n");
            abort();
        }
    }
    if (exec_fd == -1) {
        exec_fd = openat(AT_FDCWD, argv[1], O_RDONLY);
        if (exec_fd < 0) {
            dbg_print("rtld: failed to open executable\n");
            abort();
        }
        memcpy(argv, argv + 1, argc);

        //argc--;
        ((int*)argv)[-1] = argc - 1;
    }

    if ((ld_path = getenv("LD_LIBRARY_PATH")) == NULL) {
        ld_path = "/usr/lib:/lib";
    }

    init_heap();
    //dbg_print("rtld: Loading main executable... ");
    tracing = getenv("LD_TRACE_LOADED_OBJECTS") ? 1 : 0;
    main_elf = loadelf(exec_fd);
    main_elf->name = argv[0] ? argv[0] : "main executable";
    close(exec_fd);

    if (tracing) {
        char addrbuf[16];
        print("\t");
        print(main_elf->name);
        print(" (0x");
        itoax((unsigned long)main_elf->load_base, addrbuf);
        print(addrbuf);
        print(")\n");
        exit(0);
    }

    level_order_dsos();
    relocate_dsos();

    sort_dsos();

    reload_tls();

    call_init();

    return main_elf->_start;
}