#include "include/kernel.h"
#include "include/kernel_interrupts.h"
#include "include/devs.h"
#include "include/kernel_gdt_idt.h"
#include "include/kernel_sched.h"
#include "include/kernel_exec.h"
#include "include/kernel_spinlock.h"
#include "include/mm/kernel_memory.h"
#include "include/block/memdisk.h"
#include "include/kernel_spinlock.h"
#include "../libc/src/include/string.h"
#include "include/vga.h"

#pragma clang diagnostic ignored "-Wexcessive-regsave"
__attribute__((interrupt, no_caller_saved_registers)) void interr_page_fault(struct interr_frame * interrupt_frame, unsigned long error) {
    void * fault_address; // linear address
    asm volatile ("movl %%cr2, %0":"=R"(fault_address));

    if (!(error & 1)) { // caused by non-present page, see intel sdm 3A 5-55
        if (fault_address >= MEMDISKS_BASE && fault_address < MEMDISKS_BASE + MEMDISK_LIMIT_KERNEL * DEFAULT_MEMDISK_SIZE && (interrupt_frame->cs & 3) == 0) { // assuming the user cannot read memdisks themselves
            spinlock_acquire(&memdisk_lock);
            if (memdisks[GET_MEMDISK_IDX(fault_address)].used && memdisks[GET_MEMDISK_IDX(fault_address)].is_allocated) {
                paging_add_page(fault_address, PTE_PDE_PAGE_WRITABLE);
                flush_tlb_entry(fault_address);
                memset(fault_address, 0, PAGE_SIZE_NO_PAE);
                spinlock_release(&memdisk_lock);
                return;
            } else 
                spinlock_release(&memdisk_lock);
        } else { // overcommitment
            if (fault_address >= PROGRAM_HEAP_VADDR && fault_address < PROGRAM_HEAP_VADDR + PROGRAM_HEAP_SIZE) {
                paging_add_page(fault_address, PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE);
                return;
            }
            // unfortunately due to the kernel's usage pattern (cli, then kalloc), this would never trigger and we'd get a triple fault
            // else if (fault_address >= KERNEL_HEAP_BASE && fault_address <= KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE) {
            //    paging_add_page(fault_address, PTE_PDE_PAGE_WRITABLE);
            //    flush_tlb_entry(fault_address);
            //    return;
            //}
        } 
    } else { // fork() CoW
        if (fork_cow_page(fault_address)) return;
    }

    kprintf("\n\n\e41m#### ISR: Segmentation fault - Invalid memory reference! ####\nTried to reference address %p\n", fault_address);
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);

    scheduler_print_process(current_process);

    if (current_process->pid == 0) {
        panic("Kernel task cannot be recovered from a segmentation fault");
        __builtin_unreachable();
    } else if (current_process->pid == 1) {
        panic("Attempted to kill init!");
        __builtin_unreachable();
    } else {
        kprintf("Terminating process...\e0m\n");
        current_thread->status = SCHED_CLEANUP;
    }
    
    interrupt_frame->ip = kernel_idle;
    interrupt_frame->cs = GDT_KERNEL_CODE << 3;
    interrupt_frame->ss = GDT_KERNEL_DATA << 3;
}