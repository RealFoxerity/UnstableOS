// functions for manipulating an address space without going into it
#include <stdint.h>
#include <stddef.h>
#include "../../libc/src/include/string.h"
#include "../include/kernel.h"
#include "../include/mm/kernel_memory.h"


#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wvoid-pointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"

#define kprintf(fmt, ...) kprintf("MMAS: "fmt, ##__VA_ARGS__)
#define panic(fmt) panic("MMAS: "fmt)

PAGE_TABLE_TYPE * paging_get_page_table_from_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * virt_addr) { // note: maps new page for the page table, it is the caller's responsibility to unmap
    uint32_t page_directory_idx = (uint32_t)virt_addr >> 22;
    
    PAGE_TABLE_TYPE * page_table = NULL;

    if (!(pd_vaddr[page_directory_idx] & PTE_PDE_PAGE_PRESENT)) 
    {
        PAGE_TABLE_TYPE * new_page = pfalloc();

        page_table = paging_map_phys_addr_unspecified(new_page, PTE_PDE_PAGE_WRITABLE);
        memset(page_table, 0, PAGE_TABLE_ENTRIES*sizeof(PAGE_TABLE_TYPE));

        if (new_page == NULL) {
            panic("Not enough memory for page table!\n");
        }
        pd_vaddr[page_directory_idx] = (unsigned long) new_page;
        pd_vaddr[page_directory_idx] &= ~(PAGE_SIZE_NO_PAE-1);
        pd_vaddr[page_directory_idx] |= PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS;
    } else {
        page_table = paging_map_phys_addr_unspecified((void*)((unsigned long)pd_vaddr[page_directory_idx] & ~(PAGE_SIZE_NO_PAE - 1)), PTE_PDE_PAGE_WRITABLE);
    }
    return page_table;
}

extern void print_page_table_entry(void * pte);
void paging_add_page_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void *target_virt_addr, unsigned int flags) {
    PAGE_TABLE_TYPE * page_table = paging_get_page_table_from_address_space(pd_vaddr, target_virt_addr);

    uint32_t page_table_idx = (uint32_t)target_virt_addr >> 12 & (PAGE_TABLE_ENTRIES - 1);
    
    if (page_table[page_table_idx] & PTE_PDE_PAGE_PRESENT) {
        kprintf("Attempted adding a new page to already used virtual address 0x%x; pdidx: %x, ptidx: %x\nContents of page table:\n", target_virt_addr, (uint32_t)target_virt_addr >> 22, page_table_idx);
        print_page_table_entry(page_table + page_table_idx);

        panic("Tried to remap an already mapped virtual page!");
    }

    void * new_page = pfalloc();
    if (new_page == NULL) {
        panic("Not enough free memory to add a new page!\n");
    }

    page_table[page_table_idx] = ((uint32_t)new_page & ~(PAGE_SIZE_NO_PAE-1)) | (flags & (PAGE_SIZE_NO_PAE-1)) | PTE_PDE_PAGE_PRESENT;
    paging_unmap_page(page_table);
}


void paging_map_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * target_virt_addr, size_t n, unsigned int flags) {
    // align the address and size to pages
    target_virt_addr = (void*)((unsigned long)target_virt_addr&~(PAGE_SIZE_NO_PAE-1));
    n += (unsigned long)target_virt_addr & (PAGE_SIZE_NO_PAE - 1);
    if (n % PAGE_SIZE_NO_PAE != 0) n += PAGE_SIZE_NO_PAE - n % PAGE_SIZE_NO_PAE;

    size_t pages = n/PAGE_SIZE_NO_PAE;

    for (size_t i = 0; i < pages; i++) {
        paging_add_page_to_address_space(pd_vaddr, target_virt_addr + i*PAGE_SIZE_NO_PAE, flags);
    }
}


void paging_unmap_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * target_virt_addr, size_t n) {
    void * current_address_space = paging_get_address_space_paddr();
    paging_apply_address_space(paging_virt_addr_to_phys(pd_vaddr));
    paging_unmap(target_virt_addr, n);
    paging_apply_address_space(current_address_space);
}

void paging_unmap_page_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * target_virt_addr) {
    paging_unmap_to_address_space(pd_vaddr, target_virt_addr, PAGE_SIZE_NO_PAE);
}


void paging_memcpy_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * __restrict data_start_vaddr, void * __restrict data, size_t n) {
    // because the entire kernel space is copied to every process, and this function is always ran from the kernel, we can just switch address spaces
    // TODO: maybe rewrite to individually map in and out the destination pages from the new address space so we don't have to switch? kinda like in paging_add_page_to_address_space
    
    void * current_address_space = paging_get_address_space_paddr();
    paging_apply_address_space(paging_virt_addr_to_phys(pd_vaddr));
    memcpy(data_start_vaddr, data, n);
    paging_apply_address_space(current_address_space); // switch to the original
}

void paging_memset_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * data_start_vaddr, char c, size_t n) {
    // see paging_memcpy_to_address_space comment
    void * current_address_space = paging_get_address_space_paddr();
    paging_apply_address_space(paging_virt_addr_to_phys(pd_vaddr));
    memset(data_start_vaddr, 0, n);
    paging_apply_address_space(current_address_space); // switch to the original
}

void paging_memmove_to_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr, void * data_start_vaddr, char c, size_t n) {
    // see paging_memcpy_to_address_space comment
    void * current_address_space = paging_get_address_space_paddr();
    paging_apply_address_space(paging_virt_addr_to_phys(pd_vaddr));
    memmove(data_start_vaddr, 0, n);
    paging_apply_address_space(current_address_space); // switch to the original
}

PAGE_DIRECTORY_TYPE * paging_create_new_address_space() {
    
    PAGE_DIRECTORY_TYPE * page_directory_paddr = pfalloc();
    if (page_directory_paddr == NULL) {
        panic("Failed to allocate page directory for new address space!\n");
        // return NULL;
    }

    PAGE_DIRECTORY_TYPE * page_directory = paging_map_phys_addr_unspecified(page_directory_paddr, PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS);
    if (page_directory == NULL) {
        panic("Failed to map page directory for new address space!\n");
        // return NULL;
    }
    memset(page_directory, 0, PAGE_DIRECTORY_ENTRIES*sizeof(PAGE_DIRECTORY_TYPE));

    //unsigned long entries_copied = (unsigned long)kernel_mem_top/PAGE_SIZE_NO_PAE/PAGE_TABLE_ENTRIES; // copying the kernel memory map from current address space, assumes every process has the kernel mapped into it, which it should have
    //if ((unsigned long)kernel_mem_top/PAGE_SIZE_NO_PAE % PAGE_TABLE_ENTRIES != 0) entries_copied ++;
    //memcpy(page_directory, PDE_ADDR_VIRT, entries_copied*sizeof(PAGE_DIRECTORY_TYPE));

    memcpy(page_directory, PDE_ADDR_VIRT, PAGE_DIRECTORY_ENTRIES*sizeof(PAGE_DIRECTORY_TYPE)); // copying the ENTIRE kernel space, considering there shouldn't be anything extra and we need most of it for interrupts, this should be good enough

    page_directory[PAGE_DIRECTORY_ENTRIES-1] = ((unsigned long)page_directory_paddr&~(PAGE_SIZE_NO_PAE-1)) | PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS; // obv different physical address

    return page_directory;
}

void paging_print_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr) {
    if (pd_vaddr == NULL) return;
    PAGE_TABLE_TYPE * pt_vaddr;
    for (int i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        if (pd_vaddr[i] != 0) {
            pt_vaddr = paging_map_phys_addr_unspecified((void*)(pd_vaddr[i] & ~(PAGE_SIZE_NO_PAE-1)), PTE_PDE_PAGE_USER_ACCESS);
            kprintf("page table %d -> %x\n", i, (void*)(pd_vaddr[i] & ~(PAGE_SIZE_NO_PAE-1)));
            for (int j = 0; j < PAGE_TABLE_ENTRIES; j++) {
                if (pt_vaddr[j] != 0 && get_vaddr(i, j) != (void *)((unsigned long)pt_vaddr & ~(PAGE_SIZE_NO_PAE - 1))) {
                    kprintf("0x%x - 0x%x -> 0x%x - 0x%x, flags: %hx\n",
                        get_vaddr(i, j), 
                        get_vaddr(i, j) + (PAGE_SIZE_NO_PAE-1),
                        pt_vaddr[j] & ~(PAGE_SIZE_NO_PAE-1),
                        (pt_vaddr[j] & ~(PAGE_SIZE_NO_PAE-1)) + (PAGE_SIZE_NO_PAE-1),
                        (pt_vaddr[j] & (PAGE_SIZE_NO_PAE-1))
                    );
                }
            }
            paging_unmap_page(pt_vaddr);
        }
    }
}