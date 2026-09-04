#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <UnstableOS/syscalls.h>

char ** environ = NULL;
__attribute__((visibility("hidden"))) char __is_secure = 1;
__attribute__((visibility("hidden"))) char __is_rtld = 0;

extern void __attribute__((weak)) (* __init_array_start[])();
extern void __attribute__((weak)) (* __init_array_end[])();
extern void __attribute__((weak)) (* __preinit_array_start[])();
extern void __attribute__((weak)) (* __preinit_array_end[])();

extern void __attribute__((weak)) _init();
extern int __attribute__((weak)) main(int argc, char **argv, char **envp);

void __libc_init(void (*rtld_fini)(), int argc, char ** argv) {
    environ = argv + argc + 1;
    if (rtld_fini) {
        atexit(rtld_fini);
        __is_rtld = 1;
        goto skip;
    }
    if (_init)
        _init();
    for (size_t i = 0; __preinit_array_start + i < __preinit_array_end; i++)
        __preinit_array_start[i]();
    for (size_t i = 0; __init_array_start + i < __init_array_end; i++)
        __init_array_start[i]();
    skip:
    if (geteuid() != getuid() ||
        getegid() != getgid())
            __is_secure = 0;

    exit(main(argc, argv, environ));
}