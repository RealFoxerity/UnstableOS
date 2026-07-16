#include "kernel_sched.h"
#include "kernel_gdt_idt.h"
#include "kernel.h"
#include <string.h>
#include "kernel_spinlock.h"
#include "lowlevel.h"
#include "mm/kernel_memory.h"
#include <stddef.h>
#include <errno.h>
static inline int get_thread_slot(process_t * parent_process) {
    for (int i = 0; i < PTHREAD_THREADS_MAX; i++) {
        if (!PROGRAM_PCB_VADDR->thread_slots[i]) {
            PROGRAM_PCB_VADDR->thread_slots[i] = 1;
            return i;
        }
    }
    return -1;
}

// fxsave
static const unsigned short default_fx_context[] = {
    0x37F, 0, // control and status word from fninit
    0, // abridged ftw
    0, 0, 0, 0, 0, 0, 0, 0, 0, // the rest of x87 context (ops and fips)
    0x1F80, 0, // MXCSR - no flags, all SIMD exceptions masked as on poweron/reset
    0xFFFF, 0x200, // MXCSR mask
    // zeroed out ST/MM0 - ST/MM7
    // zeroed out XMM0-XMM7
};
// fnsave
static const unsigned short default_fn_context[] = {
    0x37F, 0, // control word from fninit
    0, 0, // status word
    0xFFFF, 0, // x87 empty tag from fninit
    0, 0, 0, 0, 0, 0, 0, 0 // empty pointers and ops as from fninit
    // zeroed out ST0 - ST7
};

pid_t last_tid = 0;
thread_t * kernel_create_thread(process_t * parent_process, thread_t * calling_thread, void (* entry_point)(void*), void * arg, size_t stack_guard) {
    if (stack_guard > PROGRAM_STACK_GUARD_MAX) return NULL;
    if (stack_guard == 0) stack_guard = PROGRAM_STACK_GUARD_DEFAULT;

    if (current_process == NULL) return NULL;
    if (kernel_task     == NULL) return NULL;
    if (parent_process  == NULL) return NULL;
    //kprintf("create_thread_kernel(pid: %lu), free mem: %lu\n", parent_process->pid, pf_get_free_memory());
    if (pf_get_free_memory() < sizeof(thread_t)+PROGRAM_KERNEL_STACK_SIZE) panic("Not enough memory for thread creation");

    thread_t * new = kalloc(sizeof(thread_t));
    if (new == NULL) panic("Not enough memory to allocate a new thread");

    memset(new, 0, sizeof(thread_t));
    new->instances = 1;
    new->tid = __atomic_add_fetch(&last_tid, 1, __ATOMIC_RELAXED);
    new->status = SCHED_RUNNABLE;

    new->kernel_stack = kalloc(PROGRAM_KERNEL_STACK_SIZE) + PROGRAM_KERNEL_STACK_SIZE;
    if (new->kernel_stack == NULL) {
        kfree(new);
        panic("Not enought memory to allocate new thread's kernel stack");
    }

    new->kernel_stack_size = PROGRAM_KERNEL_STACK_SIZE;

    new->context.iret_frame.ss = parent_process->ring == 0 ? (GDT_KERNEL_DATA << 3) : ((GDT_USER_DATA << 3) | 3);
    new->context.iret_frame.cs = parent_process->ring == 0 ? (GDT_KERNEL_CODE << 3) : ((GDT_USER_CODE << 3) | 3);
    new->context.iret_frame.flags = IA_32_EFL_ALWAYS_1 | IA_32_EFL_SYSTEM_INTER_EN;
    new->context.iret_frame.ip = entry_point;

    if (calling_thread != NULL)
        memcpy(new->fpu_context, calling_thread->fpu_context, sizeof(calling_thread->fpu_context));
    else {
        if (fxsave_available)
            memcpy(new->fpu_context, default_fx_context, sizeof(default_fx_context));
        else
            memcpy(new->fpu_context, default_fn_context, sizeof(default_fn_context));
    }
    new->cr3_state = parent_process->address_space_paddr;

    new->context.esp = new->kernel_stack - sizeof(struct interr_frame);
    // scheduler has to memcpy the interr_frame struct to switch context
    // without this decrement, it would overwrite next chunk's metadata
    // TODO: maybe rewrite scheduler to do it a different way?

    // 32 bit calling convention dictates arguments pushed onto stack in reverse order
    if (parent_process->ring != 0) {
        // theoretically this could be done without switching address spaces,
        // but it's insanely annoying and the performance benefit in most cases is negligible
        void * current_address_space = paging_get_address_space_paddr();

        paging_apply_address_space(parent_process->address_space_paddr);

        int thread_slot  = get_thread_slot(parent_process);
        if (thread_slot == -1) {
            paging_apply_address_space(current_address_space);
            //panic("Unable to create a userspace thread - all thread slots used up");
            kfree(new->kernel_stack);
            kfree(new);
            return NULL;
        }
        new->stack = GET_STACK_ADDR_FROM_IDX(thread_slot); // ring 0 doesn't need an iret_frame specified esp
        new->stack_size = PROGRAM_STACK_SIZE;
        new->stack_guard_size = (stack_guard + PAGE_SIZE_NO_PAE - 1) & ~(PAGE_SIZE_NO_PAE - 1);
        new->context.iret_frame.sp = new->stack;

        // TODO: This assumes that all userspace thread creation is called from the "parent thread"
        // this is so far always the case, but for the potential cases where it's not, this is wrong
        // maybe add a parent_thread argument?
        new->sa_mask = current_thread->sa_mask;
        // TODO: the thread is also supposed to inherit FPU state


        paging_map(new->context.iret_frame.sp - PROGRAM_STACK_START_SIZE,
            PROGRAM_STACK_START_SIZE, PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS);
        paging_unmap(new->stack - PROGRAM_STACK_SIZE,
            new->stack_guard_size);

        *(void**)(new->context.iret_frame.sp - sizeof(void*)) = arg;


        // setup TLS
        void * tls_bottom = GET_TLS_ADDR_FROM_IDX(thread_slot);
        paging_map(tls_bottom,
            PROGRAM_MAX_TLS_SIZE + __PROGRAM_TCB_SIZE,
            PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS
        );
        memcpy(tls_bottom, PROGRAM_TLS_BLUEPRINT_VADDR, PROGRAM_MAX_TLS_SIZE);
        new->tcb = tls_bottom + PROGRAM_MAX_TLS_SIZE;
        new->tcb->self = new->tcb;
        new->tcb->dtv_ptr = PROGRAM_DVT_VADDR;
        new->tcb->pcb = PROGRAM_PCB_VADDR;
        new->tcb->tid = new->tid;
        new->tcb->thread_slot = thread_slot;

        paging_apply_address_space(current_address_space);
    } else {
        new->context.iret_frame.sp = new->context.esp;
        new->context.esp -= 2 * sizeof(void*);

        *(void**)(new->context.iret_frame.sp - sizeof(void*)) = arg;
    }
    new->context.iret_frame.sp -= 2*sizeof(void*);
    // to be honest, i have no fucking clue why i have to do the second one
    // there probably is some 10000000 iq system v abi reason

    APPEND_DOUBLE_LINKED_LIST(new, parent_process->threads);

    //reschedule();
    return new;
}

char kernel_destroy_thread(process_t * parent_process, thread_t * thread) {
    if (thread->in_critical_section
    || thread->status == SCHED_UNINTERR_SLEEP) return 0;
    if (thread == current_thread) return 0; // we would break the current kernel heap

    kfree(thread->kernel_stack - thread->kernel_stack_size);

    /*
    // this isn't safe when multiple threads use each other's stacks, or specify addresses in them to syscalls
    // I don't think this is solvable...
    // leaving this here in case anyone decides to change the behavior

    if (parent_process->ring != 0) { // ring 0 stack is the kernel_stack
        parent_process->thread_stacks[GET_STACK_IDX_FROM_ADDR(thread->stack)] = 0;

        PAGE_DIRECTORY_TYPE * mapped_as = paging_map_phys_addr_unspecified(parent_process->address_space_paddr, PTE_PDE_PAGE_WRITABLE);

        for (void * addr = thread->stack - thread->stack_size; addr < thread->stack; ) {
            int pd_idx = (unsigned long)addr >> 22;
            kassert(mapped_as[pd_idx] & PTE_PDE_PAGE_PRESENT);

            PAGE_TABLE_TYPE * pt = paging_map_phys_addr_unspecified((void *)(unsigned long)(mapped_as[pd_idx] & ~(PAGE_SIZE_NO_PAE - 1)), PTE_PDE_PAGE_WRITABLE);
            kassert(pt);
            for (int i = ((unsigned long)addr >> 12) & ((1<<10) - 1); i < PAGE_TABLE_ENTRIES && addr < thread->stack; i++, addr += PAGE_SIZE_NO_PAE) {
                //kassert(pt[i] & PTE_PDE_PAGE_PRESENT);
                if (!(pt[i] & PTE_PDE_PAGE_PRESENT)) {
                    kprintf("Warning: Unmapped page on vaddr %p when freeing thread stack %p\n", get_vaddr(pd_idx, i), thread->stack);
                    continue;
                }
                pffree((void*)(unsigned long)(pt[i] & ~(PAGE_SIZE_NO_PAE - 1)));
            }
            paging_unmap_page(pt);
        }

        paging_unmap_to_address_space(mapped_as,
            thread->stack - thread->stack_size,
            thread->stack_size);
        paging_unmap_page(mapped_as);
    }
    */

    UNLINK_DOUBLE_LINKED_LIST(thread, parent_process->threads);

    if (__atomic_sub_fetch(&thread->instances, 1, __ATOMIC_RELEASE) == 0) kfree(thread);
    //reschedule(); // kernel_destroy_thread is meant to be ran from within schedule(), calling reschedule() would deadlock the scheduler for a given running core
    return 1;
}
