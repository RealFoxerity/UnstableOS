#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include "../multiboot.h"


enum PTE_FLAGS {
    PTE_PDE_PAGE_PRESENT = 1,
    PTE_PDE_PAGE_WRITABLE = 2,
    PTE_PDE_PAGE_USER_ACCESS = 4,
    PTE_PDE_PAGE_WRITE_THROUGH = 8, // otherwise write-back
    PTE_PDE_PAGE_DONT_CACHE = 16,
    PTE_PDE_PAGE_ACCESSED_DURING_TRANSLATE = 32,
    PTE_PAGE_DIRTY = 64, // not applicable in PDE
    //PTE_PAGE_ATTRIBUTE_TABLE = 128, // = 0
};
/*
PDE structure for 4kib page sizes
                            0 - 7                                          8 - 11           12 - 31
present, r/w, user/kernel, writethrough, cache disable, accessed, 0, 0       0          bits 12-31 of address

PTE structure for 4kib page sizes
                            0 - 7                                                 9 - 11           12 - 31
present, r/w, user/kernel, writethrough, cache disable, accessed, dirty, 0, 0       0          bits 12-31 of address

*/

#define get_vaddr(pdidx, ptidx) ((void*)(((pdidx) << 22) | ((ptidx) << 12)))

#define PAGE_SIZE_NO_PAE 0x1000
#define PAGE_DIRECTORY_TYPE uint32_t
#define PAGE_TABLE_TYPE PAGE_DIRECTORY_TYPE
#define PAGE_DIRECTORY_ENTRIES 1024
#define PAGE_TABLE_ENTRIES PAGE_DIRECTORY_ENTRIES

#define ___PDE_VADDR_BASE 0xFFFFF000
#define ___PTE_VADDR_BASE ((uint32_t)0-PAGE_TABLE_ENTRIES*PAGE_DIRECTORY_ENTRIES*sizeof(PAGE_TABLE_TYPE))
#define PDE_ADDR_VIRT ((uint32_t*)___PDE_VADDR_BASE)
//#define PTE_ADDR_VIRT_BASE ((uint32_t*)(___PDE_VADDR_BASE - (PAGE_TABLE_ENTRIES * (PAGE_DIRECTORY_ENTRIES-1) * sizeof(PAGE_TABLE_TYPE))))  // around 0xFFBFF000 normally, but we want the PDE itself to work as the last PTE, mapping all PTEs to 0xFFC00000 - 0xFFFFFFFF so that we can access them
#define PTE_ADDR_VIRT_BASE ((uint32_t*)___PTE_VADDR_BASE)

#define LOWEST_PHYS_ADDR_ALLOWABLE 0x00100000 // addresses below this are usually reserved for something, e.g. vga memory

#define IDENT_MAPPING_MAX_ADDR (PAGE_TABLE_ENTRIES*PAGE_SIZE_NO_PAE) // 4MB, sort of hardcoded

void * page_frame_alloc_init(multiboot_info_t* mbd, unsigned long free_memory, void * free_space_start_page); // returns pointer to end of frame table
unsigned long pf_get_free_memory();
void * pfalloc(); // page frame allocator, returns the amount of free space left after physical page housekeeping
void * pfalloc_1M(); // gets a 1M contiguous memory region, for dma and such
void pffree(void * page);
void pffree_1M(void * block_4M_start); // frees memory gotten by pfalloc_1M

void paging_map_phys_addr(void * src_phys_addr, void * target_virt_addr, unsigned int flags);
void * paging_map_phys_addr_unspecified(void * phys_addr, unsigned int flags); // just naively maps a physical address to nearest free virtual address

void paging_map(void * target_virt_addr, size_t n, unsigned int flags);
void paging_add_page(void * target_virt_addr, unsigned int flags); // adds a singular page, equivalent to paging_map(target_virt_addr, PAGE_SIZE_NO_PAE, flags)
void paging_unmap_page(void * virt_addr);
void paging_unmap(void * target_virt_addr, size_t n);
void paging_remap(void * old_virt_addr, void * new_virt_addr, unsigned int flags);

void * paging_virt_addr_to_phys(void * virt);
PAGE_TABLE_TYPE paging_get_pte(const void * virt_addr); // returns the value of the page table entry for a given address, 0 = not present (to check for permissions for example)

void setup_paging(unsigned long total_free, unsigned long ident_map_end);

void kalloc_prepare(void * heap_struct_start, void * heap_top);

//#pragma clang diagnostic ignored "-Wignored-attributes"
void kfree(void * p);
void * __attribute__((malloc /*, malloc(kfree)*/)) kalloc(size_t size);

void kalloc_print_heap_objects();


void paging_apply_address_space(const PAGE_DIRECTORY_TYPE * pd_paddr);
PAGE_DIRECTORY_TYPE * paging_get_address_space_paddr();

PAGE_DIRECTORY_TYPE * paging_create_new_address_space(); // returns VIRTUAL address of a new address space page directory
void paging_print_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr);
void paging_destroy_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr);

void paging_map_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * target_virt_addr, size_t n, unsigned int flags);
void paging_unmap_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * target_virt_addr, size_t n);
void paging_unmap_page_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * target_virt_addr);

void paging_memcpy_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * __restrict data_start_vaddr, void * __restrict data, size_t n); // map before calling, will throw panic
void paging_memset_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * data_start_vaddr, char c, size_t n); // map before calling, will throw panic
void paging_memmove_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * data_start_vaddr, char c, size_t n);

void enable_paging();
void disable_paging();

void flush_tlb();
void flush_tlb_entry(void * vaddr);
void flush_caches_writeback();

extern void * kernel_mem_top;
extern PAGE_DIRECTORY_TYPE * kernel_address_space_paddr;

#define kernel_address_space_vaddr ((PAGE_DIRECTORY_TYPE*)0x05FFF000) // virtual address of the kernel address space for kernel task scheduling 

#endif