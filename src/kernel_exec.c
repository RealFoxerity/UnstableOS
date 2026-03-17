#include "include/kernel_exec.h"
#include "include/errno.h"
#include "include/fs/fs.h"
#include "include/elf.h"
#include "include/kernel.h"
#include "include/kernel_interrupts.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "include/mm/kernel_memory.h"
#include "../libc/src/include/string.h"
#include "../libc/src/include/fcntl.h"


extern ssize_t exec_safe_argv_dup(char * const* argv, char * const* envp, void * stack_top_addr, char ** stack_out);

int sys_execve(const char * path, char * const* argv, char * const* envp) {
    kassert(current_process);
    kassert(current_process->pid != 0 && current_process->ring != 0); // technically we could replace the kernel, but i'd rather not

    spinlock_acquire(&scheduler_lock);
    char * stack_state = NULL;
    ssize_t stack_state_sz = exec_safe_argv_dup(argv, envp, PROGRAM_STACK_VADDR, &stack_state);
    spinlock_release(&scheduler_lock);
    if (stack_state_sz < 0) return stack_state_sz;

    int elf_fd = sys_open(path, O_RDONLY, 0);
    if (elf_fd < 0) return elf_fd;
    if (I_ISDIR(current_process->fds[elf_fd]->inode->mode)) {
        sys_close(elf_fd);
        kfree(stack_state);
        return EISDIR;
    }
    struct program new_prog = load_elf(elf_fd);
    sys_close(elf_fd);
    if (new_prog.pd_vaddr == NULL) {
        kprintf("Exec format error on attempted exec() by pid %lu tid %lu!\n", current_process->pid, current_thread->tid);
        return ENOEXEC;
    }

    // terminate threads by marking them to be cleaned up in the scheduler
    // this is basically the only way to accomplish this with SMP
    // while loop in case we race with the kernel setting eg UNINTERR_SLEEP
    while (current_process->threads->next != NULL) {
        spinlock_acquire(&scheduler_lock);
        for (thread_t * thread = current_process->threads; thread != NULL; thread = thread->next) {
            if (thread == current_thread) continue;
            thread->status = SCHED_THREAD_CLEANUP;
        }
        spinlock_release(&scheduler_lock);
        reschedule(); // allow for the termination
    }

    spinlock_acquire(&scheduler_lock);
    current_process->threads = NULL;

    if (current_process->ring != 0)
        current_process->thread_stacks[GET_STACK_IDX_FROM_ADDR(current_thread->stack)] = 0;

    // we will destroy the current address space with this mapping, thus we need the paddr
    PAGE_DIRECTORY_TYPE * new_pd_paddr = paging_virt_addr_to_phys(new_prog.pd_vaddr);
    paging_unmap_page(new_prog.pd_vaddr); // we don't want it to get pffree'd

    // both of these should be safe because all kernel stacks are inside the kernel AS which is copied
    paging_apply_address_space(new_pd_paddr);

    PAGE_DIRECTORY_TYPE * mapped_as = paging_map_phys_addr_unspecified(current_process->address_space_paddr, PTE_PDE_PAGE_WRITABLE);
    paging_destroy_address_space(mapped_as);
    paging_unmap_page(mapped_as);

    current_process->address_space_paddr = new_pd_paddr;

    //memset(current_process->thread_stacks, 0, sizeof(current_process->thread_stacks)); // shouldn't be needed

    for (int i = 0; i < PROGRAM_MAX_SEMAPHORES; i++) {
        if (current_process->semaphores[i] == NULL) continue;
        if (__atomic_sub_fetch(&current_process->semaphores[i]->used, 1, __ATOMIC_RELAXED) == 0)
            kfree(current_process->semaphores[i]);
        current_process->semaphores[i] = NULL;
    }
    current_process->signal = 0;

    thread_t * new = kernel_create_thread(current_process, new_prog.start, NULL);
    kassert(new);
    kassert(new->stack == PROGRAM_STACK_VADDR);

    kfree(new->kernel_stack - new->kernel_stack_size); // we need to preserve the current stack
    memcpy(new->stack - stack_state_sz, stack_state, stack_state_sz);
    new->context.iret_frame.sp = PROGRAM_STACK_VADDR - stack_state_sz;
    kfree(stack_state);

    // we don't need to fix new->context, it will be fixed by the scheduler
    new->kernel_stack = current_thread->kernel_stack;
    new->kernel_stack_size = current_thread->kernel_stack_size;

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

    int elf_fd = sys_open(path, O_RDONLY, 0);
    if (elf_fd < 0) return elf_fd;
    if (I_ISDIR(current_process->fds[elf_fd]->inode->mode)) {
        sys_close(elf_fd);
        return EISDIR;
    }
    struct program new_prog = load_elf(elf_fd);
    sys_close(elf_fd);
    if (new_prog.pd_vaddr == NULL) {
        kprintf("Exec format error on attempted spawn() by pid %lu tid %lu!\n", current_process->pid, current_thread->tid);
        return ENOEXEC;
    }

    spinlock_acquire(&scheduler_lock);
    process_t * proc = kalloc(sizeof(process_t));
    kassert(proc);

    // we need to copy multiple fields, so might as well copy everything
    memcpy(proc, current_process, sizeof(process_t));
    proc->ppid = current_process->pid;
    proc->pid = __atomic_add_fetch(&last_pid, 1, __ATOMIC_RELAXED);
    proc->address_space_paddr = paging_virt_addr_to_phys(new_prog.pd_vaddr);

    if (process_list->next == NULL) {
        // kernel spawning /init
        proc->ring = 3;
    }

    proc->signal = proc->exitcode = 0;
    memset(proc->semaphores, 0, sizeof(proc->semaphores));
    memset(proc->thread_stacks, 0, sizeof(proc->thread_stacks));
    memset(proc->fds, 0, sizeof(proc->fds));

    // copy stdin, stdout, stderr
    memcpy(proc->fds, current_process->fds, 3 * sizeof(file_descriptor_t *));
    for (int i = 0; i < 3; i++)
        if (proc->fds[i])
            __atomic_add_fetch(&proc->fds[i]->instances, 1, __ATOMIC_RELAXED);

    __atomic_add_fetch(&proc->pwd->instances, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&proc->root->instances, 1, __ATOMIC_RELAXED);


    proc->threads = NULL;
    thread_t * new_thread = kernel_create_thread(proc, new_prog.start, NULL);
    kassert(new_thread);
    kassert(new_thread->stack == PROGRAM_STACK_VADDR);

    paging_memcpy_to_address_space(new_prog.pd_vaddr, new_thread->stack - stack_state_sz, stack_state, stack_state_sz);
    new_thread->context.iret_frame.sp = PROGRAM_STACK_VADDR - stack_state_sz;
    kfree(stack_state);

    // relink to process_list
    APPEND_DOUBLE_LINKED_LIST(proc, process_list)

    paging_unmap_page(new_prog.pd_vaddr);

    spinlock_release(&scheduler_lock);
    return proc->pid;
}