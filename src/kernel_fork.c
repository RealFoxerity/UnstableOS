#include "include/kernel.h"
#include "include/kernel_gdt_idt.h"
#include "include/kernel_interrupts.h"
#include "include/kernel_spinlock.h"
#include "include/mm/kernel_memory.h"
#include "include/kernel_sched.h"
#include "include/kernel_exec.h"
#include "../libc/src/include/string.h"
#include <stdint.h>

// TODO: when implementing SMP, maybe a race condition with fork() and exit()?

char fork_cow_page(void * fault_address) {
    PAGE_TABLE_TYPE * pte = paging_get_pte(fault_address);
    if (*pte & PTE_FORK_WRITABLE) {
        void * old_page = paging_virt_addr_to_phys(fault_address);
        void * new_page = pfalloc_dup_page(old_page);
        kassert(new_page);

        *pte &= PAGE_SIZE_NO_PAE - 1;
        *pte &= ~PTE_FORK_WRITABLE;
        *pte |= PTE_PDE_PAGE_WRITABLE;
        *pte |= (long)new_page;
        flush_tlb_entry(fault_address);
        pffree(old_page);
        return 1;
    }
    return 0;
}

// works only for duplicating current address space
static PAGE_DIRECTORY_TYPE * fork_dup_address_space() {
    // TODO: figure out a smart way to reuse ptes
    // absolutely horrendous code, have to rewrite sometime later
    // I am sorry for anyone having to modify this in the future

    PAGE_DIRECTORY_TYPE * page_directory_phys = pfalloc();
    if (page_directory_phys == NULL) {
        panic("Failed to allocate page directory for forked address space!\n");
        // return NULL;
    }

    PAGE_DIRECTORY_TYPE * page_directory = paging_map_phys_addr_unspecified(page_directory_phys, PTE_PDE_PAGE_WRITABLE);
    kassert(page_directory);
    memset(page_directory, 0, PAGE_SIZE_NO_PAE);

    // we don't directly copy stacks and there isn't anything there after them anyway
    memcpy(page_directory, PDE_ADDR_VIRT,
        ((unsigned long)(PROGRAM_STACK_VADDR - PROGRAM_THREADS_MAX * PROGRAM_STACK_SIZE) >> 22) * sizeof(PAGE_DIRECTORY_TYPE));

    page_directory[PAGE_DIRECTORY_ENTRIES-1] =
        (unsigned long)page_directory_phys | PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_PRESENT;

    disable_wp();
    for (int i = 0; i < (unsigned long)(PROGRAM_STACK_VADDR - PROGRAM_THREADS_MAX * PROGRAM_STACK_SIZE) >> 22; i++) {
        if (!(PDE_ADDR_VIRT[i] & PTE_PDE_PAGE_PRESENT)) continue;

        // we do this & ~ because the page could get PTE_PDE_PAGE_ACCESSED_DURING_TRANSLATE
        if ((page_directory[i] & ~(PAGE_SIZE_NO_PAE - 1)) ==
            (KERNEL_ADDRESS_SPACE_VADDR[i] & ~(PAGE_SIZE_NO_PAE - 1))) continue;

        PAGE_TABLE_TYPE * ptes = PTE_ADDR_VIRT_BASE + PAGE_TABLE_ENTRIES*i;
        PAGE_TABLE_TYPE * new_ptes = pfalloc();
        kassert(new_ptes);
        page_directory[i] = (unsigned long)new_ptes |
                            PTE_PDE_PAGE_PRESENT |
                            PTE_PDE_PAGE_WRITABLE |
                            PTE_PDE_PAGE_USER_ACCESS;
        new_ptes = paging_map_phys_addr_unspecified(new_ptes, PTE_PDE_PAGE_WRITABLE);
        kassert(new_ptes);
        memset(new_ptes, 0, PAGE_TABLE_ENTRIES * sizeof(PAGE_TABLE_TYPE));

        for (int j = 0; j < PAGE_TABLE_ENTRIES; j++) {
            if (!(ptes[j] & PTE_PDE_PAGE_PRESENT)) continue;
            if (get_vaddr(i, j) == new_ptes) continue;
            if (get_vaddr(i, j) == page_directory) continue;

            void * inc_page = pfalloc_ref_inc((void*)(unsigned long)(ptes[j] & ~(PAGE_SIZE_NO_PAE - 1)));
            kassert(inc_page);

            if (ptes[j] & PTE_PDE_PAGE_WRITABLE) {
                ptes[j] &= ~PTE_PDE_PAGE_WRITABLE;
                ptes[j] |= PTE_FORK_WRITABLE;
            }

            new_ptes[j] = ptes[j];

            // if we had to duplicate
            // the new one gets the duped, old one stays
            if (inc_page != (void*)(long)(ptes[j] & ~(PAGE_SIZE_NO_PAE - 1))) {
                if (new_ptes[j] & PTE_FORK_WRITABLE) {
                    new_ptes[j] &= ~PTE_FORK_WRITABLE;
                    new_ptes[j] |= PTE_PDE_PAGE_WRITABLE;
                }
                new_ptes[j] &= PAGE_SIZE_NO_PAE - 1;
                new_ptes[j] |= (unsigned long)inc_page;
            }

            flush_tlb_entry(get_vaddr(i, j));
        }

        paging_unmap_page(new_ptes);
    }

    // duplicate the thread stack
    // we do new pages for the stack since it would be always page faulting

    int prev_pd_idx = 0; // to not call paging_map_phys_addr_unspecified over and over
    PAGE_TABLE_TYPE * pt = NULL;
    for (void * i = current_thread->stack - current_thread->stack_size;
        i < current_thread->stack;
        i += PAGE_SIZE_NO_PAE
    ) {
        void * new_page = pfalloc_dup_page(paging_virt_addr_to_phys(i));
        kassert(new_page);

        int pd_idx = (unsigned long)i >> 22;
        if (page_directory[pd_idx] == 0) {
            page_directory[pd_idx] = (long)pfalloc();
            kassert(page_directory[pd_idx]);
            PAGE_TABLE_TYPE * temp = paging_map_phys_addr_unspecified((void*)(long)page_directory[pd_idx], PTE_PDE_PAGE_WRITABLE);
            kassert(temp);
            memset(temp, 0, PAGE_TABLE_ENTRIES * sizeof(PAGE_TABLE_TYPE));
            paging_unmap_page(temp);

            page_directory[pd_idx] |= PTE_PDE_PAGE_PRESENT |
                                        PTE_PDE_PAGE_WRITABLE |
                                        PTE_PDE_PAGE_USER_ACCESS;
        }
        if (pd_idx != prev_pd_idx) {
            if (pt != NULL) paging_unmap_page(pt);
            pt = paging_map_phys_addr_unspecified(
                (void*)(unsigned long)(page_directory[pd_idx] & ~(PAGE_SIZE_NO_PAE - 1)),
                PTE_PDE_PAGE_WRITABLE);
            kassert(pt);
        }


        int pt_idx = (unsigned long)i >> 12;
        pt_idx &= PAGE_TABLE_ENTRIES - 1;

        pt[pt_idx] = (unsigned long)new_page;
        pt[pt_idx] |= PTE_PDE_PAGE_PRESENT |
                        PTE_PDE_PAGE_WRITABLE |
                        PTE_PDE_PAGE_USER_ACCESS;

    }

    paging_unmap_page(pt);
    paging_unmap_page(page_directory);
    enable_wp();

    return page_directory_phys;
}

pid_t sys_fork(context_t * ctx) {
    kassert(current_process->ring != 0); // i really don't want to deal with the kernel forking

    spinlock_acquire(&address_spaces_lock); // we need scheduler to reschedule if lockee
    spinlock_acquire(&scheduler_lock);

    process_t * new_proc = kalloc(sizeof(process_t));
    kassert(new_proc);

    memcpy(new_proc, current_process, sizeof(process_t));

    memset(new_proc->thread_stacks, 0, sizeof(new_proc->thread_stacks));
    new_proc->thread_stacks[GET_STACK_IDX_FROM_ADDR(current_thread->stack)] = 1;

    new_proc->pid = __atomic_add_fetch(&last_pid, 1, __ATOMIC_RELAXED);
    new_proc->ppid = current_process->pid;

    // "duplicate" all file descriptors and semaphores, TODO: fix the UINT32_MAX :3
    for (int i = 0; i < FD_LIMIT_PROCESS; i++) {
        if (current_process->fds[i])
            kassert(__atomic_add_fetch(&current_process->fds[i]->instances, 1, __ATOMIC_RELAXED) != UINT32_MAX);
    }

    for (int i = 0; i < PROGRAM_MAX_SEMAPHORES; i++) {
        if (current_process->semaphores[i])
            kassert(__atomic_add_fetch(&current_process->semaphores[i]->used, 1, __ATOMIC_RELAXED) != UINT32_MAX);
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
    // the physical pages are the same and their reference
    // counter is incremented

    PAGE_DIRECTORY_TYPE * pd = fork_dup_address_space();
    kassert(pd);

    new_proc->address_space_paddr = pd;
    new_thread->cr3_state = pd;


    // relink the new process
    APPEND_DOUBLE_LINKED_LIST(new_proc, process_list)

    spinlock_release(&scheduler_lock);
    spinlock_release(&address_spaces_lock);

    //scheduler_print_processes();
    return new_proc->pid;
}