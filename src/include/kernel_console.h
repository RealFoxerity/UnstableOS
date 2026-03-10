#ifndef KERNEL_CONSOLE
#define KERNEL_CONSOLE

#include <stddef.h>
#include <stdint.h>

void console_write(const char * s, size_t len);

#endif