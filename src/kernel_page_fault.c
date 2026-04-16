#include "include/debug/backtrace.h"
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

void page_fault_send_sigsegv(long was_not_mapped, mcontext_t * ctx) {
    kprintf("returning from %p\n", ctx->iret_frame.ip);
    memcpy(&current_thread->context, ctx, sizeof(mcontext_t) - ((ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *)));
    signal_thread(current_process, current_thread, &(siginfo_t){
        .si_signo = SIGSEGV,
        .si_code = was_not_mapped ? SEGV_MAPERR : SEGV_ACCERR
    });
    signal_dispatch_sa(current_process, current_thread);
    memcpy(ctx, &current_thread->context, sizeof(mcontext_t) - ((ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *)));
    kprintf("returning to %p\n", ctx->iret_frame.ip);
}

extern __attribute__((naked)) void fix_segments();
__attribute__((naked, no_caller_saved_registers)) void interr_page_fault(struct interr_frame * interrupt_frame, unsigned long error) {
    asm volatile (
        "cld;"
        "call fix_segments;"
        "cli;"
        "pushl %esp;" // the interr_frame argument
        "addl $0x4, (%esp);"

        "pushl %eax;" // preserve to read page_fault_handler return value in eax

        "call page_fault_handler;"

        "test $1, %eax;"
        "popl %eax;"
        "popl %esp;" // also skipping the error variable

        "je 0f;"
        // page fault was resolved
        "iret\n\t"

        "0:\n\t"
        // page fault wasn't resolved
        "andl $0x1, -0x4(%esp);" // the error variable
        "pusha;"
        "pushl %esp;"
        "pushf;" // because the andl changes zf based on the result
        "andl $0b1000000, (%esp);"
        "shl $6, (%esp);"

        "call page_fault_send_sigsegv;"

        "addl $0x8, %esp;"
        "popa;"
        "call reschedule;"
        "iret;"
    );
}

struct page_fault_error {
    unsigned long P    : 1;
    unsigned long W    : 1;
    unsigned long U    : 1;
    unsigned long RSVD : 1;
    unsigned long ID   : 1;
    unsigned long PK   : 1;
    unsigned long SS   : 1;
    unsigned long HLAT : 1;
    unsigned long SGX  : 1;
} __attribute__((packed));

static void print_page_fault_error(struct page_fault_error error) {
    kprintf("Page fault error: ");
    if (error.P)    kprintf("P ");
    if (error.W)    kprintf("W ");
    if (error.U)    kprintf("U ");
    if (error.RSVD) kprintf("RSVD ");
    if (error.ID)   kprintf("ID ");
    if (error.PK)   kprintf("PK ");
    if (error.SS)   kprintf("SS ");
    if (error.HLAT) kprintf("HLAT ");
    if (error.SGX)  kprintf("SGX");
    kprintf("\n");
}
__attribute__((no_caller_saved_registers)) int page_fault_handler(unsigned long __old_eax, struct interr_frame * iret_frame, struct page_fault_error error) {
    void * fault_address; // linear address
    asm volatile ("movl %%cr2, %0":"=R"(fault_address));

    if (!error.P) { // caused by non-present page, see intel sdm 3A 5-55
        if (fault_address >= MEMDISKS_BASE && fault_address < MEMDISKS_BASE + MEMDISK_LIMIT_KERNEL * DEFAULT_MEMDISK_SIZE && (iret_frame->cs & 3) == 0) { // assuming the user cannot read memdisks themselves
            spinlock_acquire(&memdisk_lock);
            if (memdisks[GET_MEMDISK_IDX(fault_address)].used && memdisks[GET_MEMDISK_IDX(fault_address)].is_allocated) {
                paging_add_page(fault_address, PTE_PDE_PAGE_WRITABLE);
                flush_tlb_entry(fault_address);
                memset(fault_address, 0, PAGE_SIZE_NO_PAE);
                spinlock_release(&memdisk_lock);
                return 1;
            } else
                spinlock_release(&memdisk_lock);
        } else { // overcommitment
            if (fault_address >= PROGRAM_HEAP_VADDR && fault_address < PROGRAM_HEAP_VADDR + PROGRAM_HEAP_SIZE) {
                paging_add_page(fault_address, PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE);
                return 1;
            }
            // unfortunately due to the kernel's usage pattern (cli, then kalloc), this would never trigger and we'd get a triple fault
            // else if (fault_address >= KERNEL_HEAP_BASE && fault_address <= KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE) {
            //    paging_add_page(fault_address, PTE_PDE_PAGE_WRITABLE);
            //    flush_tlb_entry(fault_address);
            //    return;
            //}
        }
    } else if (error.W) { // fork() CoW
        if (fork_cow_page(fault_address)) return 1;
    }
    extern spinlock_t vga_spinlock;
    vga_spinlock.state = SPINLOCK_UNLOCKED;
    kprintf("\n\e[0m\e[41m\n#### ISR: Segmentation fault - Invalid memory reference! ####\nTried to reference address %p\n", fault_address);
    print_page_fault_error(error);
    print_interr_frame(iret_frame);

    scheduler_print_process(current_process);
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));

    if (!error.U) { // kernel space caused the page fault
        //kalloc_print_heap_objects();
        panic("Kernel task cannot be recovered from a segmentation fault");
        __builtin_unreachable();
    }
    kprintf("\e[0m\n");

    return 0;
}