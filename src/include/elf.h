#ifndef ELF_H
#define ELF_H

#include <UnstableOS/elf.h>

void readelf(void * start, size_t size);


#include "mm/kernel_memory.h"
#include "kernel_sched.h"
#include "fs/fs.h"
struct program load_elf(int * status, const char * path, char * const* argv, char * const* envp, int elf_fd);
char check_elf(file_descriptor_t * file); // returns 1 if elf is not truncated or broken, generic test, not supported test
#endif