#include "include/kernel_sched.h"
#include "include/kernel_gdt_idt.h"
#include "include/kernel.h"
#include "../libc/src/include/string.h"
#include "include/lowlevel.h"
#include "include/mm/kernel_memory.h"
#include <stddef.h>

static inline void * get_thread_stack(process_t * parent_process) {
    for (int i = 0; i < PROGRAM_THREADS_MAX; i++) {
        if (!parent_process->thread_stacks[i]) {
            parent_process->thread_stacks[i] = 1;
            return GET_STACK_ADDR_FROM_IDX(i);
        }
    }
    return NULL;
}

pid_t last_tid = 0;
thread_t * kernel_create_thread(process_t * parent_process, void (* entry_point)(void*), void * arg) {
    kprintf("create_thread_kernel(pid: %lu), free mem: %lu\n", parent_process->pid, pf_get_free_memory());
    if (pf_get_free_memory() < sizeof(thread_t)+PROGRAM_KERNEL_STACK_SIZE) panic("Not enough memory for thread creation");

    thread_t * new = kalloc(sizeof(thread_t));
    if (new == NULL) panic("Not enough memory to allocate a new thread");

    memset(new, 0, sizeof(thread_t));
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
    new->cr3_state = paging_virt_addr_to_phys(parent_process->address_space_vaddr);

    new->context.esp = new->kernel_stack - sizeof(struct interr_frame); 
    // scheduler has to memcpy the interr_frame struct to switch context
    // without this decrement, it would overwrite next chunk's metadata
    // TODO: maybe rewrite scheduler to do it a different way?

    // 32 bit calling convention dictates arguments pushed onto stack in reverse order
    if (parent_process->ring != 0) {
        new->stack = get_thread_stack(parent_process); // ring 0 doesn't need an iret_frame specified esp
        new->stack_size = PROGRAM_STACK_SIZE;
        if (new->stack == NULL) panic("Unable to create thread - all thread slots used up");
        new->context.iret_frame.sp = new->stack;
        paging_map_to_address_space(parent_process->address_space_vaddr, new->context.iret_frame.sp - PROGRAM_STACK_SIZE, PROGRAM_STACK_SIZE, PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS);
    } else {
        new->context.iret_frame.sp = new->context.esp;
        new->context.esp -= 2 * sizeof(void*);
    }
    new->context.iret_frame.sp -= sizeof(void*); // without this, = arg is written above 0xF000'0000
    *(void**)new->context.iret_frame.sp = arg;
    new->context.iret_frame.sp -= sizeof(void*); 
    // to be honest, i have no fucking clue why i have to do this
    // there probably is some 10000000 iq system v abi reason

    if (parent_process->threads == NULL) 
        parent_process->threads = new;
    else {
        parent_process->threads->prev->next = new;
        new->prev = parent_process->threads->prev;
    }
    parent_process->threads->prev = new;

    //reschedule();
    return new;
}

void kernel_destroy_thread(process_t * parent_process, thread_t * thread) {
    kfree(thread->kernel_stack - thread->kernel_stack_size);
    
    if (parent_process->ring != 0) { // ring 0 stack is the kernel_stack
        parent_process->thread_stacks[GET_STACK_IDX_FROM_ADDR(thread->stack)] = 0;
        paging_unmap_to_address_space(parent_process->address_space_vaddr, 
            thread->stack - thread->stack_size, 
            thread->stack_size);
    }
    
    if (thread->next != NULL) thread->next->prev = thread->prev;
    else parent_process->threads->prev = thread->prev; // is at the end process->prev->next handled by next line

    if (thread != parent_process->threads) thread->prev->next = thread->next;
    else parent_process->threads = thread->next; // metadata fixed in process->next != NULL

    kfree(thread);
    //reschedule(); // kernel_destroy_thread is meant to be ran from within schedule(), calling reschedule() would deadlock the scheduler for a given running core
}
