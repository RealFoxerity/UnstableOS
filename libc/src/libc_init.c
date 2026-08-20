#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <UnstableOS/syscalls.h>

char ** environ = NULL;

extern void (* __init_array_start[])();
extern void (* __init_array_end[])();
extern void (* __preinit_array_start[])();
extern void (* __preinit_array_end[])();

extern void _init();
void __libc_init(int (*main)(int argc, char **argv, char **envp), int argc, char ** argv) {
    environ = argv + argc + 1;
    _init();
    for (size_t i = 0; __preinit_array_start + i < __preinit_array_end; i++)
        __preinit_array_start[i]();
    for (size_t i = 0; __init_array_start + i < __init_array_end; i++)
        __init_array_start[i]();
    exit(main(argc, argv, environ));
}