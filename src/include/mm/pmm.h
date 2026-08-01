#pragma once

#include <stddef.h>
#include "../multiboot.h"

#ifndef USE_LEGACY_PFA
void pmm_init_pre_vmm(const multiboot_info_t* multiboot_info);
void pmm_init_post_vmm();
size_t pmm_alloc(size_t order);
void pmm_free(size_t page_num);
void pmm_retain(size_t page_num);
size_t pmm_get_usable_pages_end();
size_t pmm_get_usable_page_count();
size_t pmm_get_free_page_count();
#define pmm_size_to_order(size) ({ kassert(size != 0); __builtin_ctz(size / PAGE_SIZE); })
#endif
