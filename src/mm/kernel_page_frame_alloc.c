#include "../include/mm/kernel_memory.h"
#include "../include/multiboot.h"
#include <stdint.h>
#include <stddef.h>
#include "../../libc/src/include/string.h"
#include "../include/kernel.h"

// TODO: implement locking!

#define kprintf(fmt, ...) kprintf("PF: "fmt, ##__VA_ARGS__)

#define PFALLOC_UNUSED 0
#define PFALLOC_UNUSABLE 1

static uint8_t * page_frame_table = NULL;
static void * page_frame_table_start_addr = NULL;
static unsigned long page_frame_table_entries = 0;


#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wvoid-pointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"

#define RESERVED_PAGE_FRAMES_END (PAGE_SIZE_NO_PAE*PAGE_SIZE_NO_PAE) // 16MiB "reserved" for DMA allocations, will try to avoid to allocate for normal allocations if possible

void * page_frame_alloc_init(multiboot_info_t* mbd, unsigned long free_memory, void * free_space_start_page) { // we may lose up to PAGE_SIZE_NO_PAE if the amount of memory taken up by PFT itself would lower the max entries and bring it the table down by one page
    if (((unsigned long)free_space_start_page)%PAGE_SIZE_NO_PAE != 0) { // we need to deal with pages, so free memory has to be page aligned
        free_space_start_page += PAGE_SIZE_NO_PAE - ((unsigned long)free_space_start_page%PAGE_SIZE_NO_PAE);
        free_memory -= PAGE_SIZE_NO_PAE - ((unsigned long)free_space_start_page%PAGE_SIZE_NO_PAE);
    }
    
    page_frame_table = free_space_start_page;

    page_frame_table_entries = free_memory/PAGE_SIZE_NO_PAE; // we may lose < 1 PAGE_SIZE_NO_PAE worth of memory
    unsigned long pft_size_frames = page_frame_table_entries/PAGE_SIZE_NO_PAE + (page_frame_table_entries%PAGE_SIZE_NO_PAE)!=0?1:0;

    page_frame_table_start_addr = free_space_start_page + pft_size_frames*PAGE_SIZE_NO_PAE;

    page_frame_table_entries -= pft_size_frames; //NOTE: qemu shows a single page being lost at the very top of the address space

    memset(page_frame_table, PFALLOC_UNUSED, page_frame_table_entries);

    for (int i = 0; i < mbd->mmap_length; i+= sizeof(multiboot_memory_map_t)) {
        multiboot_memory_map_t* mmmt = (multiboot_memory_map_t*) (mbd->mmap_addr + i);
        if (mmmt->addr + mmmt->len < (unsigned long)page_frame_table_start_addr) continue;
        if (mmmt->type != MULTIBOOT_MEMORY_AVAILABLE) {
            unsigned long start = mmmt->addr;
            if (start < (unsigned long)page_frame_table + page_frame_table_entries) start = (unsigned long)page_frame_table + page_frame_table_entries;
            if (start % PAGE_SIZE_NO_PAE != 0) start = start + PAGE_SIZE_NO_PAE - start%PAGE_SIZE_NO_PAE;

            unsigned long end = mmmt->addr + mmmt->len;
            if (end%PAGE_SIZE_NO_PAE != 0) end = end + PAGE_SIZE_NO_PAE - end%PAGE_SIZE_NO_PAE;
            
            for (int i = start; i < end; i += PAGE_SIZE_NO_PAE) {
                if (end > (unsigned long)page_frame_table_start_addr+page_frame_table_entries*PAGE_SIZE_NO_PAE) break; // shouldn't happen if kernel.c memory detection works
                page_frame_table[(i-(unsigned long)page_frame_table_start_addr)/PAGE_SIZE_NO_PAE] = PFALLOC_UNUSABLE;
            }
        }
    }
    kprintf("Managing %lu pages from %p to %p\n", page_frame_table_entries, page_frame_table_start_addr, page_frame_table_start_addr+page_frame_table_entries*PAGE_SIZE_NO_PAE);

    return page_frame_table_start_addr + page_frame_table_entries;
}


//static void refill_pft_cache() { // todo: add next X free pages cache
//
//}

void * pfalloc_dup_page(void * page) {
    if (page < page_frame_table_start_addr) {
        kprintf("Warning: Tried to duplicate a frame outside (below) of managed range!\n");
        return NULL;
    }
    if (page > page_frame_table_start_addr + page_frame_table_entries*PAGE_SIZE_NO_PAE) {
        kprintf("Warning: Tried to duplicate a frame outside (above) of managed range!\n");
        return NULL;
    }
    int page_index = (page - page_frame_table_start_addr)/PAGE_SIZE_NO_PAE;
    if (page_frame_table[page_index] == PFALLOC_UNUSED) {
        kprintf("Warning: Tried to duplicate a freed frame!\n");
        return NULL;
    }


    void * new_frame = pfalloc();
    kassert(new_frame);
    void * mapped_new = paging_map_phys_addr_unspecified(new_frame, PTE_PDE_PAGE_WRITABLE);
    kassert(mapped_new);
    void * mapped_old = paging_map_phys_addr_unspecified(page, PTE_PDE_PAGE_WRITABLE);
    kassert(mapped_old);

    memcpy(mapped_new, mapped_old, PAGE_SIZE_NO_PAE);
    paging_unmap_page(mapped_new);
    paging_unmap_page(mapped_old);

    return new_frame;
}

void * pfalloc_ref_inc(void * page) {
    if (page < page_frame_table_start_addr) {
        kprintf("Warning: Tried to increment reference counter for a frame outside (below) of managed range!\n");
        return NULL;
    }
    if (page > page_frame_table_start_addr + page_frame_table_entries*PAGE_SIZE_NO_PAE) {
        kprintf("Warning: Tried to increment reference counter for a frame outside (above) of managed range!\n");
        return NULL;
    }
    int page_index = (page - page_frame_table_start_addr)/PAGE_SIZE_NO_PAE;
    if (page_frame_table[page_index] == PFALLOC_UNUSED) {
        kprintf("Warning: Tried to increment reference counter for a freed frame!\n");
        return NULL;
    }
    if (page_frame_table[page_index] == UINT8_MAX) {
        void * new_page = pfalloc_dup_page(page);
        return new_page;
    } else {
        __atomic_add_fetch(&page_frame_table[page_index], 1, __ATOMIC_RELAXED);
        return page;
    }
}

void * pfalloc() {
    if (page_frame_table_entries > RESERVED_PAGE_FRAMES_END) {
        for (int i = RESERVED_PAGE_FRAMES_END; i < page_frame_table_entries; i++) {
            if (page_frame_table[i] == PFALLOC_UNUSED) {
                page_frame_table[i] = 1;
                return page_frame_table_start_addr + i*PAGE_SIZE_NO_PAE;
            }
        }
    }
    for (int i = page_frame_table_entries - 1; i >= 0; i--) {
        if (page_frame_table[i] == PFALLOC_UNUSED) {
            page_frame_table[i] = 1;
            return page_frame_table_start_addr + i*PAGE_SIZE_NO_PAE;
        }
    }
    return NULL;
}

#define PAGE_COUNT_1M 256 // 256*4096 = 1M
void * pfalloc_1M() {
    for (int i = 0; i < page_frame_table_entries; i++) {
        next_iter:
        if (page_frame_table[i] == PFALLOC_UNUSED) {
            if (page_frame_table_entries - i < PAGE_COUNT_1M) return NULL;
            
            for (int j = 0; j < PAGE_COUNT_1M; j++) {
                if (page_frame_table[i+j] == 1) {
                    i+=j+1;
                    goto next_iter;
                }
            }

            for (int j = 0; j < PAGE_COUNT_1M; j++) {
                page_frame_table[i+j] = 1;
            }
            return page_frame_table_start_addr + i*PAGE_SIZE_NO_PAE;
        }
    }
    return NULL;
}

void pffree(void *page) {
    if (page < page_frame_table_start_addr) {
        kprintf("Warning: Tried to free page frame outside (below) of managed range paddr %p!\n", page);
        return;
    }
    if (page > page_frame_table_start_addr + page_frame_table_entries*PAGE_SIZE_NO_PAE) {
        kprintf("Warning: Tried to free page frame outside (above) of managed range paddr %p!\n", page);
        return;
    }
    int page_index = (page - page_frame_table_start_addr)/PAGE_SIZE_NO_PAE;
    if (page_frame_table[page_index] == PFALLOC_UNUSED) {
        kprintf("Warning: Tried to double free a paddr %p!\n", page);
        return;
    }
    __atomic_sub_fetch(&page_frame_table[page_index], 1, __ATOMIC_RELAXED);
}

void pffree_1M(void * block_4M_start) {
    if (block_4M_start < page_frame_table_start_addr) {
        kprintf("Warning: Tried to free 1M block outside (below) of managed range paddr %p!\n", block_4M_start);
        return;
    }
    if (block_4M_start > page_frame_table_start_addr + page_frame_table_entries*PAGE_SIZE_NO_PAE) {
        kprintf("Warning: Tried to free 1M block outside (above) of managed range paddr %p!\n", block_4M_start);
        return;
    }
    int page_index = (block_4M_start - page_frame_table_start_addr)/PAGE_SIZE_NO_PAE;
    for (int i = 0; i < PAGE_COUNT_1M; i++) {
        if (page_frame_table[page_index+i] == PFALLOC_UNUSED) kprintf("Warning: Tried to double free element %d of 1M block paddr %p\n", page_index+i, block_4M_start);
        __atomic_sub_fetch(&page_frame_table[page_index+i], 1, __ATOMIC_RELAXED);
    }
}

unsigned long pf_get_free_memory() {
    unsigned long free_mem = 0;
    for (int i = 0; i < page_frame_table_entries; i++) if (page_frame_table[i] == PFALLOC_UNUSED) free_mem += PAGE_SIZE_NO_PAE;
    return free_mem;
}