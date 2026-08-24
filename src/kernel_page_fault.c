#include "debug/backtrace.h"
#include "kernel.h"
#include "kernel_interrupts.h"
#include <UnstableOS/devs.h>
#include "kernel_gdt_idt.h"
#include "kernel_sched.h"
#include "kernel_exec.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include "block/memdisk.h"
#include "kernel_spinlock.h"
#include <string.h>
#include "gfx/vga.h"
#include "lowlevel.h"
#pragma clang diagnostic ignored "-Wexcessive-regsave"

void page_fault_send_sigsegv(long was_not_mapped, __gregcontext_t * ctx) {
    void * fault_address;
    asm volatile ("movl %%cr2, %0":"=R"(fault_address));
    if (fxsave_available)
        asm volatile(
            "fxsave %0"
            ::"m"(current_thread->fpu_context)
        );
    else
        asm volatile(
            "fnsave %0"
            ::"m"(current_thread->fpu_context)
        );
    memcpy(&current_thread->context, ctx, sizeof(__gregcontext_t) - ((ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *)));
    signal_thread(current_process, current_thread, &(siginfo_t){
        .si_signo = SIGSEGV,
        .si_code = was_not_mapped ? SEGV_MAPERR : SEGV_ACCERR,
        .si_addr = fault_address
    });
    signal_dispatch_sa(current_process, current_thread);
    memcpy(ctx, &current_thread->context, sizeof(__gregcontext_t) - ((ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *)));
}

void page_fault_send_sigbus(__gregcontext_t * ctx) {
    if (fxsave_available)
        asm volatile(
            "fxsave %0"
            ::"m"(current_thread->fpu_context)
        );
    else
        asm volatile(
            "fnsave %0"
            ::"m"(current_thread->fpu_context)
        );
    memcpy(&current_thread->context, ctx, sizeof(__gregcontext_t) - ((ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *)));
    signal_thread(current_process, current_thread, &(siginfo_t){
        .si_signo = SIGBUS,
        .si_code = BUS_OBJERR,
    });
    signal_dispatch_sa(current_process, current_thread);
    memcpy(ctx, &current_thread->context, sizeof(__gregcontext_t) - ((ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *)));
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

        "test $2, %eax;"
        "jnz 1f;"

        "test $1, %eax;"
        "jz 0f;"

        "popl %eax;"
        "popl %esp;" // also skipping the error variable
        // page fault was resolved
        "iret\n\t"

        "0:\n\t"
        // page fault wasn't resolved - sigsegv
        "popl %eax;"
        "popl %esp;"
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
        "iret;\n"

        "1:\n"
        // page fault wasn't resolved - sigbus BUS_OBJERR
        "popl %eax;"
        "popl %esp;"
        "pusha;"
        "pushl %esp;"
        "call page_fault_send_sigbus;"
        "popl %esp;"
        "popa;"
        "call reschedule;"
        "iret;"
    );
}

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
// 0 = sigsegv, 1 = ok, 2 = sigbus
__attribute__((no_caller_saved_registers)) int page_fault_handler(unsigned long __old_eax, struct interr_frame * iret_frame, struct page_fault_error error) {
    void * fault_address; // linear address
    asm volatile ("movl %%cr2, %0":"=R"(fault_address));

    if (!error.P) { // caused by non-present page, see intel sdm 3A 5-55
        if (fault_address >= MEMDISKS_BASE && fault_address < MEMDISKS_BASE + MEMDISK_LIMIT_KERNEL * DEFAULT_MEMDISK_SIZE && (iret_frame->cs & 3) == 0) { // assuming the user cannot read memdisks themselves
            spinlock_acquire(&memdisk_lock);
            if (memdisks[GET_MEMDISK_IDX(fault_address)].used && memdisks[GET_MEMDISK_IDX(fault_address)].is_allocated) {
                spinlock_release(&memdisk_lock);
                if (paging_add_page(fault_address, PTE_PDE_PAGE_WRITABLE) == NULL) {
                    kprintf("\e[0m\e[41mPage fault: Ran out of memory in memdisk overcommitment! Killing task...\n");
                    current_process->do_cleanup = 1;
                    return 0;
                } else {
                    flush_tlb_entry(fault_address);
                    memset(fault_address, 0, PAGE_SIZE_NO_PAE);
                }
                return 1;
            } else
                spinlock_release(&memdisk_lock);
        } else { // overcommitment
            // heap
            if (fault_address >= PROGRAM_HEAP_VADDR && fault_address <= current_process->program_break) {
                if (paging_add_page(fault_address, PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE) == NULL) {
                    kprintf("\e[0m\e[41mPage fault: Ran out of memory in heap overcommitment! Killing task...\n");
                    current_process->do_cleanup = 1;
                    return 0;
                } else return 1;
                // stack
            } else if (fault_address < current_thread->stack &&
                fault_address >= current_thread->stack - PROGRAM_STACK_SIZE + current_thread->stack_guard_size) {
                if (paging_add_page(fault_address, PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE) == NULL) {
                    kprintf("\e[0m\e[41mPage fault: Ran out of memory in stack overcommitment! Killing task...\n");
                    current_process->do_cleanup = 1;
                    return 0;
                } else return 1;
            }
            char res = mmap_page_fault(fault_address, error);
            if (res == 0) return 1; // sorry :sob:
            if (res == -1) return 2;
        }
    } else if (error.W) { // fork() CoW
        if (fork_cow_page(fault_address)) return 1;
    }
    if (!error.U) { // don't wanna unnecessarily break the spinlocks
        gfx_spinlock.state = SPINLOCK_UNLOCKED;
        framebuffer_lock.state = SPINLOCK_UNLOCKED;
        back_framebuffer_w = 0;
        back_framebuffer_h = 0;
        back_framebuffer = NULL;
    }
    kprintf("\n\e[0m\e[41m\n#### ISR: Segmentation fault - Invalid memory reference! ####\nTried to reference address %p\n", fault_address);
    kprintf("\nCR3: %p\n", paging_get_address_space_paddr());
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