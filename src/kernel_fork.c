#include "include/kernel.h"
#include "include/kernel_gdt_idt.h"
#include "include/kernel_interrupts.h"
#include "include/kernel_spinlock.h"
#include "include/mm/kernel_memory.h"
#include "include/kernel_sched.h"
#include "include/kernel_exec.h"
#include "../libc/src/include/string.h"

static PAGE_DIRECTORY_TYPE * paging_duplicate_address_space(PAGE_DIRECTORY_TYPE * old_pd_vaddr) {
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
    memcpy(page_directory, old_pd_vaddr, PAGE_DIRECTORY_ENTRIES*sizeof(PAGE_DIRECTORY_TYPE));

    page_directory[PAGE_DIRECTORY_ENTRIES-1] = ((unsigned long)page_directory_paddr&~(PAGE_SIZE_NO_PAE-1)) | PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS; // obv different physical address




    return page_directory;
}

pid_t sys_fork(context_t * ctx) {
    kassert(current_process->ring != 0); // i really don't want to deal with the kernel forking

    kprintf("Fork is not yet supported!\n");
    return -1;

    spinlock_acquire(&scheduler_lock);

    process_t * new_proc = kalloc(sizeof(process_t));
    kassert(new_proc);

    memcpy(new_proc, current_process, sizeof(process_t));

    // old semaphores don't make much sense in a new process
    memset(new_proc->semaphores, 0, sizeof(new_proc->semaphores));

    memset(new_proc->thread_stacks, 0, sizeof(new_proc->thread_stacks));
    new_proc->thread_stacks[GET_STACK_IDX_FROM_ADDR(current_thread->stack)] = 1;

    new_proc->pid = __atomic_add_fetch(&last_pid, 1, __ATOMIC_RELAXED);
    new_proc->ppid = current_process->pid;

    // "duplicate" all file descriptors
    for (int i = 0; i < FD_LIMIT_PROCESS; i++) {
        if (current_process->fds[i])
            __atomic_add_fetch(&current_process->fds[i]->instances, 1, __ATOMIC_RELAXED);
    }

    __atomic_add_fetch(&current_process->pwd->instances, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&current_process->root->instances, 1, __ATOMIC_RELAXED);

    thread_t * new_thread = kalloc(sizeof(thread_t));
    kassert(new_thread);

    memcpy(new_thread, current_thread, sizeof(thread_t));

    new_thread->tid = __atomic_add_fetch(&last_tid, 1, __ATOMIC_RELAXED);
    new_thread->next = NULL;
    new_thread->prev = new_thread;
    new_thread->kernel_stack = kalloc(current_thread->kernel_stack_size) + 
                                current_thread->kernel_stack_size;
    kassert(new_thread->kernel_stack);
    
    memcpy(&new_thread->context, ctx, sizeof(context_t));
    new_thread->context.eax = 0; // returning 0 from the fork

    // see kernel_sched.c for description of how we handle context switching
    new_thread->context.esp = new_thread->context.iret_frame.sp;

    new_thread->status = SCHED_RUNNABLE; // copied state would be SCHED_RUNNING

    new_proc->threads = new_thread;

    // generate a new address space
    // this is done by duplicating the current one and
    // marking everything writable read-only and then embedding the actual
    // protection level into the free bits of the PTE
    // we do new pages for the stacks since they would be always page faulting
    // the final physical pages in the PTEs are the same and their reference
    // counter is incremented
    // TODO maybe reuse the PTE themselves?

    // mark everything in this address space the same way

    // readjust the new program's and threads' cr3 to point to the new one

    // relink the new process
    APPEND_DOUBLE_LINKED_LIST(new_proc, process_list)

    spinlock_release(&scheduler_lock);
    //scheduler_print_processes();
    return new_proc->pid;
}