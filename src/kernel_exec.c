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

#include "include/kernel_gdt_idt.h"

static __attribute__((naked)) void exec_jump(void * new_esp) {
    asm volatile (
        "pushl %eax\n\t"
        // right so for simple arguments, gcc decides that stuff should
        // be in registers eax, edx... we are in C, there's a convention
        // called cdecl... and it says... everything onto the stack
        // why does gcc fucking do this???
        // AND IT'S NOT EVEN CONSISTENT!
        // in kernel_interrupts.c we have pic_send_eoi with uint8_t
        // and that one IS passed on the stack
        // fuck me, fuck gcc, fuck this hobby
        // rant over

        "xor %eax, %eax\n\t" // zero out everything
        "xor %ebx, %ebx\n\t"
        "xor %ecx, %ecx\n\t"
        "xor %edx, %edx\n\t"
        "xor %edi, %edi\n\t"
        "xor %esi, %esi\n\t"
        "popl %esp; iret;"
    );
}

int sys_exec(const char * path) {
    kassert(current_process);
    kassert(current_process->pid != 0); // technically we could replace the kernel, but i'd rather not

    int elf_fd = sys_open(path, O_RDONLY, 0);
    if (elf_fd < 0) return elf_fd;
    if (I_ISDIR(current_process->fds[elf_fd]->inode->mode)) {
        sys_close(elf_fd);
        return EISDIR;
    }
    struct program new_prog = load_elf(elf_fd);
    sys_close(elf_fd);
    if (new_prog.pd_vaddr == NULL) {
        kprintf("Exec format error on attempted exec() by pid %lu tid %lu!\n", current_process->pid, current_thread->tid);
        return ENOEXEC;
    }
    spinlock_acquire(&scheduler_lock);

    // TODO: when adding SMP, ensure no threads are running
    while (current_process->threads != NULL) {
        if (current_process->threads == current_thread) { // we need to move the kernel stack around
            current_process->threads = current_process->threads->next;
            continue;
        }
        kernel_destroy_thread(current_process, current_process->threads);
    }

    if (current_process->ring != 0)
        current_process->thread_stacks[GET_STACK_IDX_FROM_ADDR(current_thread->stack)] = 0;


    // both of these should be safe because all kernel stacks are inside the kernel AS which is copied
    paging_apply_address_space(paging_virt_addr_to_phys(new_prog.pd_vaddr));
    paging_destroy_address_space(current_process->address_space_vaddr);

    //memset(current_process->thread_stacks, 0, sizeof(current_process->thread_stacks)); // shouldn't be needed
    memset(current_process->semaphores, 0, sizeof(current_process->semaphores));
    current_process->signal = 0;

    current_process->address_space_vaddr = new_prog.pd_vaddr;

    thread_t * new = kernel_create_thread(current_process, new_prog.start, NULL);
    kfree(new->kernel_stack - new->kernel_stack_size); // we need to preserve the current stack
    
    // we don't need to fix new->context, it will be fixed by the scheduler
    new->kernel_stack = current_thread->kernel_stack;
    new->kernel_stack_size = current_thread->kernel_stack_size;
    
    kfree(current_thread);
    
    current_thread = new;

    void * target = new->kernel_stack - sizeof(struct interr_frame);

    if (current_process->ring == 0) {
        target += 2*sizeof(void*);
        new->context.iret_frame.sp = new->kernel_stack;
        memcpy(target, &new->context.iret_frame, sizeof(struct interr_frame) - 2 * sizeof(void*));
    }
    else {
        memcpy(target, &new->context.iret_frame, sizeof(struct interr_frame));
    }

    spinlock_release(&scheduler_lock);
    exec_jump(target);
    __builtin_unreachable();
}