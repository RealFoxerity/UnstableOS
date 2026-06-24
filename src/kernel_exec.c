#include "kernel_exec.h"
#include <errno.h>
#include "fs/fs.h"
#include "elf.h"
#include "kernel.h"
#include "kernel_interrupts.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#include "kernel_tty_io.h"
#include "mm/kernel_memory.h"
#include <string.h>
#include <fcntl.h>


extern ssize_t exec_safe_argv_dup(char * const* argv, char * const* envp, void * stack_top_addr, char ** stack_out);

int sys_execve(const char * path, char * const* argv, char * const* envp) {
    kassert(current_process);
    kassert(current_process->pid != 0 && current_process->ring != 0); // technically we could replace the kernel, but i'd rather not

    char * stack_state = NULL;
    ssize_t stack_state_sz = exec_safe_argv_dup(argv, envp, PROGRAM_STACK_VADDR, &stack_state);
    if (stack_state_sz < 0) return stack_state_sz;

    int elf_fd = sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (elf_fd < 0) return elf_fd;
    if (S_ISDIR(current_process->fds[elf_fd]->inode->mode)) {
        sys_close(elf_fd);
        kfree(stack_state);
        return -EISDIR;
    }
    struct program new_prog = load_elf(elf_fd);
    sys_close(elf_fd);
    if (new_prog.pd_vaddr == NULL) {
        kprintf("Exec format error on attempted exec() by pid %lu tid %lu!\n", current_process->pid, current_thread->tid);
        return -ENOEXEC;
    }

    CRIT_SEC_START

    // terminate threads by marking the process to be cleaned up in the scheduler
    while (current_process->threads->next != NULL) {
        current_process->do_cleanup = 1; // just in case
        reschedule(); // allow for the termination
    }

    for (int i = 0; i < FD_LIMIT_PROCESS; i++) {
        if (current_process->fds[i] && current_process->fd_flags[i] & (O_CLOEXEC >> 12)) {
            sys_close(i);
        }
    }

    spinlock_acquire(&scheduler_lock);

    current_process->do_cleanup = 0;

    current_process->threads = NULL; // effectively doubles as CRIT_SEC_END
    for (struct rt_siginfo_ll * freed = current_process->sa_rt_queue; freed != NULL; ) {
        struct rt_siginfo_ll * next = freed->next;
        kfree(freed);
        freed = next;
    }
    current_process->sa_rt_queue_last  = NULL;
    current_process->sa_rt_queue_count = 0;
    current_process->sa_pending = 0;
    memset(current_process->sa_pending_info, 0, sizeof(current_process->sa_pending_info));
    memset(current_process->sa_handlers, 0, sizeof(current_process->sa_handlers));

    memset(&PROGRAM_PCB_VADDR->thread_slots, 0, PTHREAD_THREADS_MAX * sizeof(char));

    // we will destroy the current address space with this mapping, thus we need the paddr
    PAGE_DIRECTORY_TYPE * new_pd_paddr = paging_virt_addr_to_phys(new_prog.pd_vaddr);
    paging_unmap_page(new_prog.pd_vaddr); // we don't want it to get pffree'd

    // both of these should be safe because all kernel stacks are inside the kernel AS which is copied
    paging_apply_address_space(new_pd_paddr);

    PAGE_DIRECTORY_TYPE * mapped_as = paging_map_phys_addr_unspecified(current_process->address_space_paddr, PTE_PDE_PAGE_WRITABLE);
    paging_destroy_address_space(mapped_as);
    paging_unmap_page(mapped_as);

    current_process->address_space_paddr = new_pd_paddr;
    current_process->program_break = PROGRAM_HEAP_VADDR;

    current_process->after_exec = 1;
    current_process->pending_waiting = 0;

    for (int i = 0; i < SEM_NSEMS_MAX; i++) {
        if (current_process->semaphores[i] == NULL) continue;
        if (__atomic_sub_fetch(&current_process->semaphores[i]->used, 1, __ATOMIC_RELAXED) == 0)
            kfree(current_process->semaphores[i]);
        current_process->semaphores[i] = NULL;
    }

    thread_t * new = kernel_create_thread(current_process, new_prog.start, NULL, 0);
    kassert(new);
    kassert(new->stack == PROGRAM_STACK_VADDR);

    kfree(new->kernel_stack - new->kernel_stack_size); // we need to preserve the current stack
    memcpy(new->stack - stack_state_sz, stack_state, stack_state_sz);
    new->context.iret_frame.sp = PROGRAM_STACK_VADDR - stack_state_sz;
    kfree(stack_state);

    // we don't need to fix new->context, it will be fixed by the scheduler
    new->kernel_stack = current_thread->kernel_stack;
    new->kernel_stack_size = current_thread->kernel_stack_size;
    new->sa_mask = current_thread->sa_mask;

    if (__atomic_sub_fetch(&current_thread->instances, 1, __ATOMIC_RELAXED) == 0)
        kfree(current_thread);

    current_thread = new;

    void * target = new->kernel_stack - sizeof(struct interr_frame);

    /*
    if (current_process->ring == 0) {
        target += 2*sizeof(void*);
        new->context.iret_frame.sp = new->kernel_stack;
        memcpy(target, &new->context.iret_frame, sizeof(struct interr_frame) - 2 * sizeof(void*));
    }
    else {
        memcpy(target, &new->context.iret_frame, sizeof(struct interr_frame));
    }*/
    memcpy(target, &new->context.iret_frame, sizeof(struct interr_frame));

    spinlock_release(&scheduler_lock);

    current_thread->in_critical_section = 0;

    asm volatile ( // check the note in kernel_syscall.c
        "mov %0, %%ds;"
        "mov %0, %%es;"
        "mov %0, %%fs;"
        "mov %0, %%gs;"
        "pushl %1\n\t" // save the new esp
        "xor %%eax, %%eax\n\t" // zero out everything
        "xor %%ebx, %%ebx\n\t"
        "xor %%ecx, %%ecx\n\t"
        "xor %%edx, %%edx\n\t"
        "xor %%edi, %%edi\n\t"
        "xor %%esi, %%esi\n\t"
        "popl %%esp; iret;"
        ::  "R"(new->context.iret_frame.ss),
            "R"(target)
    );
    __builtin_unreachable();
}

int sys_spawn(const char *path, char * const* argv, char * const* envp) {
    kassert(current_process);

    spinlock_acquire(&scheduler_lock);
    char * stack_state = NULL;
    ssize_t stack_state_sz = exec_safe_argv_dup(argv, envp, PROGRAM_STACK_VADDR, &stack_state);
    if (stack_state_sz < 0) return stack_state_sz;
    spinlock_release(&scheduler_lock);

    int elf_fd = sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (elf_fd < 0) return elf_fd;
    if (S_ISDIR(current_process->fds[elf_fd]->inode->mode)) {
        sys_close(elf_fd);
        return -EISDIR;
    }
    struct program new_prog = load_elf(elf_fd);
    sys_close(elf_fd);
    if (new_prog.pd_vaddr == NULL) {
        kprintf("Exec format error on attempted spawn() by pid %lu tid %lu!\n", current_process->pid, current_thread->tid);
        return -ENOEXEC;
    }

    spinlock_acquire(&scheduler_lock);
    process_t * proc = kalloc(sizeof(process_t));
    kassert(proc);

    spinlock_acquire(&current_process->lock);
    // we need to copy multiple fields, so might as well copy everything
    memcpy(proc, current_process, sizeof(process_t));
    proc->lock.state = SPINLOCK_UNLOCKED;
    if (current_process->pgrp_leader) {
        __atomic_add_fetch(&current_process->pgrp_leader->pgrp_members, 1, __ATOMIC_RELAXED);
    }
    spinlock_release(&current_process->lock);

    proc->after_exec = 1;
    proc->pending_waiting = 0;
    proc->next_alarm = 0;
    proc->user_clicks = proc->system_clicks = proc->dead_user_clicks = proc->dead_system_clicks = 0;
    proc->parent = current_process;
    proc->pid = __atomic_add_fetch(&last_pid, 1, __ATOMIC_RELAXED);
    proc->address_space_paddr = paging_virt_addr_to_phys(new_prog.pd_vaddr);
    proc->program_break = PROGRAM_HEAP_VADDR;
    proc->threads = NULL;

    proc->sa_pending = 0;
    for (struct rt_siginfo_ll * freed = proc->sa_rt_queue; freed != NULL; ) {
        struct rt_siginfo_ll * next = freed->next;
        kfree(freed);
        freed = next;
    }
    proc->sa_rt_queue_last  = NULL;
    proc->sa_rt_queue_count = 0;
    memset(proc->sa_pending_info, 0, sizeof(proc->sa_pending_info));
    memset(proc->sa_handlers, 0, sizeof(proc->sa_handlers));

    if (process_list->next == NULL) {
        // kernel spawning /init
        proc->ring = 3;
        proc->pgrp = proc->pid; // 1, note that you can't signal to pgrp 1 either
        proc->session = proc->pid;
        terminals[DEV_TTY_0]->foreground_pgrp = proc->pgrp;
        terminals[DEV_TTY_0]->session = proc->session;

        init_task = proc;
    }

    memset(proc->semaphores, 0, sizeof(proc->semaphores));


    // to not leak kernel fds on spawn
    memset(proc->fds, 0, sizeof(proc->fds));
    if (process_list->next == NULL)
        memcpy(proc->fds, current_process->fds, 3 * sizeof(file_descriptor_t *));
    else
        memcpy(proc->fds, current_process->fds, FD_LIMIT_PROCESS * sizeof(file_descriptor_t *));

    for (int i = 0; i < FD_LIMIT_PROCESS; i++)
        if (proc->fds[i] && !(proc->fd_flags[i] & (O_CLOEXEC >> 12)) && !(proc->fd_flags[i] & (O_CLOFORK >> 12)))
            __atomic_add_fetch(&proc->fds[i]->instances, 1, __ATOMIC_RELAXED);
        else proc->fds[i] = NULL;

    __atomic_add_fetch(&proc->pwd->instances, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&proc->root->instances, 1, __ATOMIC_RELAXED);


    thread_t * new_thread = kernel_create_thread(proc, new_prog.start, NULL, 0);
    kassert(new_thread);
    kassert(new_thread->stack == PROGRAM_STACK_VADDR);

    new_thread->sa_mask = current_thread->sa_mask;
    new_thread->in_critical_section = 0;

    paging_memcpy_to_address_space(new_prog.pd_vaddr, new_thread->stack - stack_state_sz, stack_state, stack_state_sz);
    new_thread->context.iret_frame.sp = PROGRAM_STACK_VADDR - stack_state_sz;
    kfree(stack_state);

    // relink to process_list
    APPEND_DOUBLE_LINKED_LIST(proc, process_list)

    paging_unmap_page(new_prog.pd_vaddr);

    spinlock_release(&scheduler_lock);
    return proc->pid;
}