#include <stdint.h>
#include <stddef.h>
#include "../../libc/src/include/string.h"
#include "../../libc/src/include/stdio.h" // sprintf
#include "../include/kernel.h"
#include "../include/mm/kernel_memory.h"
#include "../include/vga.h"

#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wvoid-pointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"

// NOTE: add error checking for when (somehow???) all of the 4G memory range is available and thus we wouldn't be able to map everything without PAE

#define dkprintf(fmt, ...) kprintf("MM: "fmt, ##__VA_ARGS__)
#define dsprintf(s, fmt, ...) sprintf(s, "MM: "fmt, ##__VA_ARGS__)

#define dpanic(fmt) panic("MM: "fmt)
//static void * paging_phys_addr_to_virt(void * phys) { // doesn't make sense
//    
//}

void flush_caches_writeback() {
    asm volatile ("wbinvd");
}

void flush_tlb() {
    asm volatile ("movl %cr3, %eax; movl %eax, %cr3");
}

void flush_tlb_entry(void * vaddr) {
    vaddr = (void*)((unsigned long)vaddr&~(PAGE_SIZE_NO_PAE-1));
    asm volatile (
        "invlpg (%0)"
        ::"R"(vaddr));
}

void print_page_table_entry(const void * pte) {
    dkprintf("PTE at %x: phys addr %x | Flags: ", pte, *(uint32_t*)pte & (~(PAGE_SIZE_NO_PAE-1)));

    if ( *(uint32_t*)pte & PTE_PDE_PAGE_PRESENT) {
        kprintf("Present, ");
    }
    if ( *(uint32_t*)pte & PTE_PDE_PAGE_WRITABLE) {
        kprintf("Writable, ");
    }
    if ( *(uint32_t*)pte & PTE_PDE_PAGE_USER_ACCESS) {
        kprintf("UA, ");
    }
    if ( *(uint32_t*)pte & PTE_PDE_PAGE_WRITE_THROUGH) {
        kprintf("CWT, ");
    }
    if ( *(uint32_t*)pte & PTE_PDE_PAGE_DONT_CACHE) {
        kprintf("DC, ");
    }
    if ( *(uint32_t*)pte & PTE_PDE_PAGE_ACCESSED_DURING_TRANSLATE) {
        kprintf("PADT, ");
    }
    if ( *(uint32_t*)pte & PTE_PAGE_DIRTY) {
        kprintf("Dirty, ");
    }
    kprintf("\n");
}


static PAGE_TABLE_TYPE * paging_get_page_table(void * virt_addr) {
    uint32_t page_directory_idx = (uint32_t)virt_addr >> 22;
    
    if (!(PDE_ADDR_VIRT[page_directory_idx] & PTE_PDE_PAGE_PRESENT)) 
    {
        PAGE_TABLE_TYPE * new_page = pfalloc();
        if (new_page == NULL) {
            dpanic("Not enough memory for page table!\n");
        }
        PDE_ADDR_VIRT[page_directory_idx] = (unsigned long) new_page;
        PDE_ADDR_VIRT[page_directory_idx] &= ~(PAGE_SIZE_NO_PAE-1);
        PDE_ADDR_VIRT[page_directory_idx] |= PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS; // PDE permissions override PTE permissions
        memset(PTE_ADDR_VIRT_BASE + PAGE_TABLE_ENTRIES * page_directory_idx, 0, PAGE_TABLE_ENTRIES*sizeof(PAGE_TABLE_TYPE));
    }
    return PTE_ADDR_VIRT_BASE + PAGE_TABLE_ENTRIES * page_directory_idx;
}

void paging_map_phys_addr(void * src_phys_addr, void * target_virt_addr, unsigned int flags) {  
    PAGE_TABLE_TYPE * page_table = paging_get_page_table(target_virt_addr);
    uint32_t page_table_idx = (uint32_t)target_virt_addr >> 12 & (PAGE_TABLE_ENTRIES - 1);

    if (page_table[page_table_idx] & PTE_PDE_PAGE_PRESENT) {
        dkprintf("Attempted mapping address 0x%x to already used virtual address 0x%x; pdidx: %x, ptidx: %x\nContents of page table:\n", src_phys_addr, target_virt_addr, (uint32_t)target_virt_addr >> 22, page_table_idx);
        print_page_table_entry(page_table + page_table_idx);

        dpanic("Illegal MMU operation");
    }

    page_table[page_table_idx] = ((uint32_t)src_phys_addr & (~(PAGE_SIZE_NO_PAE-1))) | (flags & (PAGE_SIZE_NO_PAE-1)) | PTE_PDE_PAGE_PRESENT;
    flush_tlb_entry(target_virt_addr);
}

PAGE_TABLE_TYPE paging_get_pte(const void * virt_addr) {
    uint32_t page_directory_idx = (uint32_t)virt_addr >> 22;
    uint32_t page_table_idx = (uint32_t)virt_addr >> 12 & (PAGE_TABLE_ENTRIES - 1);

    if (!(PDE_ADDR_VIRT[page_directory_idx] & PTE_PDE_PAGE_PRESENT)) return 0;

    PAGE_TABLE_TYPE * pt = PTE_ADDR_VIRT_BASE + PAGE_TABLE_ENTRIES * page_directory_idx;
    
    if (!(pt[page_table_idx] & PTE_PDE_PAGE_PRESENT)) return 0;

    return pt[page_table_idx];
}

void paging_remap(void * old_virt_addr, void * new_virt_addr, unsigned int flags) {
    void * phys_addr = paging_virt_addr_to_phys(old_virt_addr);
    paging_unmap_page(old_virt_addr);
    paging_map_phys_addr(phys_addr, new_virt_addr, flags);
}

void paging_add_page(void * target_virt_addr, unsigned int flags) {
    PAGE_TABLE_TYPE * page_table = paging_get_page_table(target_virt_addr);
    uint32_t page_table_idx = (uint32_t)target_virt_addr >> 12 & (PAGE_TABLE_ENTRIES - 1);
    
    if (page_table[page_table_idx] & PTE_PDE_PAGE_PRESENT) {
        dkprintf("Warning: Attempted adding a new page to already used virtual address 0x%x; pdidx: %x, ptidx: %x\nContents of page table:\n", target_virt_addr, (uint32_t)target_virt_addr >> 22, page_table_idx);
        print_page_table_entry(page_table + page_table_idx);
        
        //dpanic("Illegal MMU operation");
        return;
    }

    void * new_page = pfalloc();
    if (new_page == NULL) {
        dpanic("Not enough free memory to add a new page!\n");
    }

    page_table[page_table_idx] = ((uint32_t)new_page & (~(PAGE_SIZE_NO_PAE-1))) | (flags & (PAGE_SIZE_NO_PAE-1)) | PTE_PDE_PAGE_PRESENT;
    flush_tlb_entry(target_virt_addr);
}

void paging_map(void * target_virt_addr, size_t n, unsigned int flags) {
    // align the address and size to pages
    target_virt_addr = (void*)((unsigned long)target_virt_addr&~(PAGE_SIZE_NO_PAE-1));
    n += (unsigned long)target_virt_addr & (PAGE_SIZE_NO_PAE - 1);
    if (n % PAGE_SIZE_NO_PAE != 0) n += PAGE_SIZE_NO_PAE - n % PAGE_SIZE_NO_PAE;

    size_t pages = n/PAGE_SIZE_NO_PAE;

    for (size_t i = 0; i < pages; i++) {
        paging_add_page(target_virt_addr + i*PAGE_SIZE_NO_PAE, flags);
    }
}

void paging_change_flags(void * target_virt_addr, size_t n, unsigned int flags) {
    target_virt_addr = (void*)((unsigned long)target_virt_addr&~(PAGE_SIZE_NO_PAE-1));
    n += (unsigned long)target_virt_addr & (PAGE_SIZE_NO_PAE - 1);
    if (n % PAGE_SIZE_NO_PAE != 0) n += PAGE_SIZE_NO_PAE - n % PAGE_SIZE_NO_PAE;

    size_t pages = n/PAGE_SIZE_NO_PAE;

    for (size_t i = 0; i < pages; i++) {
        PAGE_TABLE_TYPE * page_table = paging_get_page_table(target_virt_addr);
        uint32_t page_table_idx = (uint32_t)target_virt_addr >> 12 & (PAGE_TABLE_ENTRIES - 1);
        if (!(page_table[page_table_idx] & PTE_PDE_PAGE_PRESENT)) {
            dkprintf("Attempted changing flags on nonexistent page at vaddr 0x%x!\n", target_virt_addr);
            dpanic("Illegal MMU operation");
            __builtin_unreachable();
        }

        page_table[page_table_idx] &= ~(PAGE_SIZE_NO_PAE-1); // zero out flags
        page_table[page_table_idx] |= flags & (PAGE_SIZE_NO_PAE-1);
        page_table[page_table_idx] |= PTE_PDE_PAGE_PRESENT;
        flush_tlb_entry(target_virt_addr);

        target_virt_addr += PAGE_SIZE_NO_PAE;
    }
}

void paging_unmap_page(void * virt_addr) {
    PAGE_TABLE_TYPE * page_table = paging_get_page_table(virt_addr);
    uint32_t page_table_idx = (uint32_t)virt_addr >> 12 & (PAGE_TABLE_ENTRIES - 1);

    //if (!(page_table[page_table_idx] & PTE_PDE_PAGE_PRESENT)) {
    //    dkprintf("Tried to unmap an already unmapped virtual page at requested vaddr 0x%x!\n", virt_addr);
    //    dpanic("Illegal MMU operation");
    //}
    page_table[page_table_idx] = 0;

    flush_tlb_entry(virt_addr);
}

void paging_unmap(void * target_virt_addr, size_t n) {
    // align the address and size to pages
    target_virt_addr = (void*)((unsigned long)target_virt_addr&~(PAGE_SIZE_NO_PAE-1));
    n += (unsigned long)target_virt_addr & (PAGE_SIZE_NO_PAE - 1);
    if (n % PAGE_SIZE_NO_PAE != 0) n += PAGE_SIZE_NO_PAE - n % PAGE_SIZE_NO_PAE;

    size_t pages = n/PAGE_SIZE_NO_PAE;

    for (size_t i = 0; i < pages; i++) {
        paging_unmap_page(target_virt_addr + i*PAGE_SIZE_NO_PAE);
    }
}

void * paging_map_phys_addr_unspecified(void * phys_addr, unsigned int flags) {
    phys_addr = (void *)((unsigned long)phys_addr & ~(PAGE_SIZE_NO_PAE - 1));

    PAGE_TABLE_TYPE * pt;
    for (int i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        if (!(PDE_ADDR_VIRT[i] & PTE_PDE_PAGE_PRESENT)) { // init space for new page table if missing
            PAGE_TABLE_TYPE * new_page = pfalloc();
            if (new_page == NULL) {
                dpanic("Not enough memory for page table!\n");
            }
            PDE_ADDR_VIRT[i] = (unsigned long) new_page;
            PDE_ADDR_VIRT[i] &= ~(PAGE_SIZE_NO_PAE-1);
            PDE_ADDR_VIRT[i] |= PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE;
            memset(PTE_ADDR_VIRT_BASE + PAGE_TABLE_ENTRIES * i, 0, PAGE_TABLE_ENTRIES*sizeof(PAGE_TABLE_TYPE));
        }
        for (int j = 0; j < PAGE_TABLE_ENTRIES; j++) {
            pt = PTE_ADDR_VIRT_BASE + PAGE_TABLE_ENTRIES * i + j;
            if (__builtin_expect(!(*pt & PTE_PDE_PAGE_PRESENT), 0)) {
                *pt = (unsigned long) phys_addr;
                *pt |= PTE_PDE_PAGE_PRESENT | flags;
                flush_tlb_entry(get_vaddr(i,j));
                return get_vaddr(i, j);
            }
        }
    }
    return NULL;
}

void * paging_virt_addr_to_phys(void * virt) {
    uint32_t page_directory_idx = (uint32_t)virt >> 22;
    uint32_t page_table_idx = ((uint32_t)virt >> 12) & (PAGE_TABLE_ENTRIES - 1);

    if (!(PDE_ADDR_VIRT[page_directory_idx] & PTE_PDE_PAGE_PRESENT)) return NULL;

    uint32_t * page_table = PTE_ADDR_VIRT_BASE + PAGE_TABLE_ENTRIES * page_directory_idx; // pointer arithmetic automatically scales by size of member

    if (!(page_table[page_table_idx] & PTE_PDE_PAGE_PRESENT)) return NULL;
    
    return (void *)((page_table[page_table_idx] & (~(PAGE_SIZE_NO_PAE-1))) + ((uint32_t)virt & (PAGE_SIZE_NO_PAE-1)));
}


static inline char is_in_bounds_pae(unsigned long val, unsigned long lower, unsigned long upper) {
    if (val > lower && val < upper) return 1;
    if (val + PAGE_SIZE_NO_PAE > lower && val < upper) return 1;
    return 0;
}

void enable_wp() {
    asm volatile (
        "mov %cr0, %eax\n\t"
        "or $0x00010000, %eax\n\t" // wp bit (16)
        "mov %eax, %cr0\n\t"
    );
}
void disable_wp() {
    asm volatile (
        "mov %cr0, %eax\n\t"
        "and $0xFFFEFFFF, %eax\n\t"
        "mov %eax, %cr0\n\t"
    );
}

void enable_paging() {
    asm volatile (
        "mov %cr0, %eax\n\t"
        "or $0x80000001, %eax\n\t" // paging bit + protected bit
        "mov %eax, %cr0\n\t"
    );
}
void disable_paging() {
    asm volatile (
        "mov %cr0, %eax\n\t"
        "and $0x7FFFFFFF, %eax\n\t" // inverted paging bit
        "mov %eax, %cr0\n\t"
    );
}
void paging_apply_address_space(const PAGE_DIRECTORY_TYPE * pd_paddr) { // doesn't need tlb flush because any write to cr3 flushes tlb automatically
    asm volatile (
        "movl %0, %%cr3"
        :
        : "R" (pd_paddr)
    );
}
PAGE_DIRECTORY_TYPE * paging_get_address_space_paddr() {
    PAGE_DIRECTORY_TYPE * out;
    asm volatile (
        "movl %%cr3, %0"
        :"=R"(out)
    );
    return out;
}

void paging_destroy_address_space(PAGE_DIRECTORY_TYPE * pd_vaddr) {
    if (pd_vaddr == NULL) return;
    for (int i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        if ((pd_vaddr[i] & ~(PAGE_SIZE_NO_PAE - 1)) != (PDE_ADDR_VIRT[i]& ~(PAGE_SIZE_NO_PAE - 1)) && pd_vaddr[i] != 0) { // we need to be careful around the kernel addresses
            pffree((void*) ((unsigned long)pd_vaddr[i] & ~(PAGE_SIZE_NO_PAE-1)));
        }
    }
    //pffree(paging_virt_addr_to_phys(pd_vaddr)); 
    // no need to free the pd since thats always the last pde, thus being freed at the last for loop iteration
    
    
    paging_unmap_page(pd_vaddr);
}


static unsigned long ident_map_top = IDENT_MAPPING_MAX_ADDR;

PAGE_DIRECTORY_TYPE * kernel_address_space_paddr = NULL;

void setup_paging(unsigned long total_free, unsigned long ident_map_end) {
    if (ident_map_end < IDENT_MAPPING_MAX_ADDR) ident_map_end = IDENT_MAPPING_MAX_ADDR;
    if (ident_map_end % PAGE_SIZE_NO_PAE != 0) ident_map_end += PAGE_SIZE_NO_PAE - (ident_map_end%PAGE_SIZE_NO_PAE);
    ident_map_top = ident_map_end;


    if (total_free < 1<<21) panic("At least 3MB of memory is required for basic kernel functionality!\n");

    PAGE_DIRECTORY_TYPE * page_directory = pfalloc();
    if (page_directory == NULL) {
        dpanic("Not enough memory for page directory!\n");
    }
    memset(page_directory, 0, PAGE_DIRECTORY_ENTRIES*sizeof(PAGE_DIRECTORY_TYPE));
    kprintf("Identity mapping 0x%x - 0x%x...\n", 0, ident_map_top); // see TODO below, this will be wrong
    for (int i = 0; i < ident_map_top/PAGE_SIZE_NO_PAE/PAGE_TABLE_ENTRIES + (ident_map_top/PAGE_SIZE_NO_PAE % PAGE_TABLE_ENTRIES != 0)?1:0; i++) {
        page_directory[i] = (uint32_t)pfalloc();
        if (page_directory[i] == 0) {
            dpanic("Not enough memory for identity mapping!\n");
        }
        memset((void*)page_directory[i], 0, PAGE_TABLE_ENTRIES*sizeof(PAGE_TABLE_TYPE));
        for (int j = 0; j < PAGE_TABLE_ENTRIES; j++) { // TODO: fix, otherwise identity mapping is always aligned to 4M boundaries, potentially wasting "a lot" of memory
            ((uint32_t*)page_directory[i])[j] = (i*PAGE_TABLE_ENTRIES + j) * PAGE_SIZE_NO_PAE;
            ((uint32_t*)page_directory[i])[j] |= PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE;
        }
        page_directory[i] &= ~(PAGE_SIZE_NO_PAE-1);
        page_directory[i] |= PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE; 
        // check whether writable is really needed here, note: ia-32 allows writes anywhere in ring 0 unless bit 16 of cr0 is set
    }

    page_directory[PAGE_DIRECTORY_ENTRIES-1] = ((unsigned long)page_directory&~(PAGE_SIZE_NO_PAE-1)) | PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE;
    
    kernel_address_space_paddr = page_directory;

    dkprintf("Enabling paging...\n");

    paging_apply_address_space(kernel_address_space_paddr);
    enable_paging();
    paging_map_phys_addr(page_directory, KERNEL_ADDRESS_SPACE_VADDR, PTE_PDE_PAGE_WRITABLE);

    dkprintf("Remapping memory areas...\n");

    // VGA text mode cache remap, won't do anything, but in case we setup write-through sometimes, vga has to be write-back otherwise huge performance penalty
    paging_change_flags((void*)(unsigned long)VGA_TEXT_MODE_ADDR, VGA_WIDTH*VGA_HEIGHT*sizeof(uint16_t), PTE_PDE_PAGE_WRITABLE);

    dkprintf("Enabling WP bit...\n");
    enable_wp();

    dkprintf("Setting up kernel heap...\n");

    paging_map(KERNEL_HEAP_BASE, KERNEL_HEAP_START_SIZE, PTE_PDE_PAGE_WRITABLE);

    kalloc_prepare(KERNEL_HEAP_BASE, KERNEL_HEAP_BASE + KERNEL_HEAP_START_SIZE, KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE);
    if (KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE > kernel_mem_top) kernel_mem_top = KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE;

    dkprintf("Mapped vmemory 0x%x to 0x%x, alloc. mem: %d\n", KERNEL_HEAP_BASE, KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE, KERNEL_HEAP_SIZE);
}