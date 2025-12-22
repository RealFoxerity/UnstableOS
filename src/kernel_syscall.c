#include <stdint.h>

#include "include/kernel_interrupts.h"
#include "include/kernel.h"
#include "../libc/src/include/string.h"
#include "include/kernel_tty.h"
#include "include/mm/kernel_memory.h"
#include "include/vga.h"
#include "include/errno.h"
#include "include/rs232.h"
#include "include/kernel_sched.h"
#include "include/kernel_semaphore.h"
#include "include/fs/fs.h"

#define kprintf(fmt, ...) kprintf("Kernel Routines: "fmt, ##__VA_ARGS__)

extern void clear_screen_fatal(); // kernel_interrupts.c

extern uint8_t vga_x, vga_y;
#define ssize_t long

static char check_address_range(const void * addr, size_t n, char writable) { // check whether the address range is inside the program, is mapped, and whether it is writable (assumes correct address space)
    n += (unsigned long)addr & (PAGE_SIZE_NO_PAE - 1);
    addr = (void*)((unsigned long) addr & ~(PAGE_SIZE_NO_PAE - 1));

    for (const void * iteraddr = addr; iteraddr < addr+n && iteraddr >= addr; iteraddr += PAGE_SIZE_NO_PAE) { // > addr in case we wrap around
        PAGE_TABLE_TYPE pte = paging_get_pte(iteraddr);
        if (pte == 0) return 0; // page not present, would cause page fault

        if ((PAGE_DIRECTORY_TYPE*)iteraddr >= PTE_ADDR_VIRT_BASE) return 0; // even though this is theoretically a valid kernel operation, probably not intended
        if (current_process->pid != 0) {
            if (!(pte & PTE_PDE_PAGE_USER_ACCESS)) return 0;
            if (iteraddr <= kernel_mem_top) return 0;
            
            if (writable && !(pte & PTE_PDE_PAGE_WRITABLE)) return 0; 
            // the intel architecture allows writes into unwritable memory in ring 0 (see bit 16 of cr0), 
            // this would normally be a check inside the kernel too, but due to the way we map the programs in
            // this would disallow the write() into the new address space
        }

    }
    return 1;
}

#define SYSCALL_PANIC_TEXT " #### RING 2 INDUCED PANIC; HALTING #### "


extern process_t * process_list;
long sys_getpgid(pid_t target_pid) {
    if (target_pid == 0) return current_process->pgrp;

    process_t * tested = process_list;
    while (tested != NULL) {
        if (tested->pid == target_pid) break;
        tested = tested->next;
    }
    if (tested != NULL) return tested->pgrp; // found the pid
    else return ESRCH;
}

long kernel_syscall_dispatcher();
__attribute__((naked, no_caller_saved_registers)) void interr_syscall(struct interr_frame * interrupt_frame) {
    asm volatile (
        "push %ebp;"
        "mov %esp, %ebp;"
        "call kernel_syscall_dispatcher;"
        "pop %ebp;"
        "iret;"
    );
}

long kernel_syscall_dispatcher() {
    long return_value = ENOSYS;
    
    enum syscalls syscall_number;
    long arg1, arg2, arg3;

    asm volatile (
        "movl %%eax, %0;"
        "movl %%edi, %1;"
        "movl %%esi, %2;"
        "movl %%edx, %3;"
        :"=m"(syscall_number), "=m"(arg1), "=m"(arg2), "=m"(arg3):
        :"eax", "edi", "esi", "edx"
    );

    // keep code below assignments otherwise the registers could be overwritten

    switch (syscall_number) {
        case SYSCALL_YIELD:
            current_thread->inside_kernel = 1;
            asm volatile ("sti;");
            reschedule();
            break;
        case SYSCALL_CREATE_THREAD:
            kernel_create_thread(current_process, (void*)arg1, (void*)arg2); // theoretically don't have to check bounds since they would just cause a segmentation fault
            break;
        case SYSCALL_EXIT_THREAD: // returns exitcode
            kprintf("thread %d of process %d called thread_exit()\n", current_thread->tid, current_process->pid);
            current_thread->status = SCHED_THREAD_CLEANUP;
            asm volatile ("sti;");
            reschedule();
            asm volatile ("jmp kernel_idle");
            break;

        case SYSCALL_EXIT: // returns exitcode
            kprintf("process called exit(%d)\n", arg1);
            current_process->exitcode = arg1;
        case SYSCALL_ABORT:
            if (syscall_number == SYSCALL_ABORT) kprintf("Thread %d of process %d called abort()!\n", current_thread->tid, current_process->pid); // so that we can keep the fall-through for syscall_exit
            current_thread->status = SCHED_CLEANUP;
            asm volatile ("sti;");
            reschedule();
            asm volatile ("jmp kernel_idle");
            break;
        
        case SYSCALL_WRITE:
            if (!check_address_range((const void*)arg2, arg3, 0)) {
                return_value = EFAULT;
                break;
            }
            current_thread->inside_kernel = 1;
            asm volatile ("sti;");
            return_value =  sys_write(arg1, (const void*)arg2, arg3);
            break;
        case SYSCALL_READ:
            if (!check_address_range((void*)arg2, arg3, 1)) {
                return_value = EFAULT;
                break;
            }
            current_thread->inside_kernel = 1;
            asm volatile ("sti;");
            return_value = sys_read(arg1, (void*)arg2, arg3);
            break;
        case SYSCALL_INTERR_RING2_PANIC:
            if (current_process->ring > 2 && !current_thread->inside_kernel) break; // has to be at least ring 2 or has to be called within a kernel routine
        
            clear_screen_fatal();
            vga_x = VGA_WIDTH/2 - (sizeof(SYSCALL_PANIC_TEXT)-1)/2, vga_y = VGA_HEIGHT/2;
            kprintf(SYSCALL_PANIC_TEXT);
            asm volatile ("cli; hlt;");
            break;

        case SYSCALL_SEM_INIT:
            for (int i = 0; i < PROGRAM_MAX_SEMAPHORES; i++) {
                if (!current_process->semaphores[i].used) {
                    memset(&current_process->semaphores[i].waiting_queue, 0, sizeof(thread_queue_t));
                    current_process->semaphores[i].used = 1;
                    current_process->semaphores[i].value = arg1;
                    return_value = i;
                    goto syscall_exit;
                }
            }
            return_value = ENOLCK;
            break;
        case SYSCALL_SEM_POST:
            if (arg1 < 0 || arg1 >= PROGRAM_MAX_SEMAPHORES) {
                return_value = EINVAL;
                break;
            }
            if (!current_process->semaphores[arg1].used) {
                return_value = EINVAL;
                break;
            }
            if (current_process->semaphores[arg1].value + 1 == 0) { // TODO: fix for atomicity?
                return_value = ERANGE;
                break;
            }
            current_thread->inside_kernel = 1;
            asm volatile ("sti;");

            kernel_sem_post(current_process, arg1);
            kprintf("sem posted\n");
            break;
        case SYSCALL_SEM_WAIT:
            if (arg1 < 0 || arg1 >= PROGRAM_MAX_SEMAPHORES) {
                return_value = EINVAL;
                break;
            }
            if (!current_process->semaphores[arg1].used) {
                kprintf("Called sem_wait on invalid (unused) semaphore (%d)\n", arg1);

                return_value = EINVAL;
                break;
            }
            current_thread->inside_kernel = 1;
            asm volatile ("sti;");

            kernel_sem_wait(current_process, current_thread, arg1);
            kprintf("sem waited\n");
            break;
        case SYSCALL_SEM_DESTROY:
            if (arg1 < 0 || arg1 >= PROGRAM_MAX_SEMAPHORES) {
                return_value = EINVAL;
                break;
            }
            if (!current_process->semaphores[arg1].used) {
                return_value = EINVAL;
                break;
            }
            current_process->semaphores[arg1].used = 0;
            break;
        case SYSCALL_GETPID:
            return_value = current_process->pid;
            break;
        case SYSCALL_GETPGID:
            sys_getpgid((pid_t)arg1);
            break;
        case SYSCALL_SETPGID:
            break;
        case SYSCALL_MOUNT:
            if (!check_address_range((const void*)arg1, 1, 0)) {
                return_value = EFAULT;
                break;
            }
            return_value = sys_mount((const char*)arg1, (const char*)arg2, arg3);
            break;
        case SYSCALL_TCGETPGRP:
        case SYSCALL_TCSETPGRP:
        case SYSCALL_OPEN:
            current_thread->inside_kernel = 1;
            asm volatile ("sti;");
            break;
        case SYSCALL_CLOSE:
            current_thread->inside_kernel = 1;
            asm volatile ("sti;");
            break;
        case SYSCALL_MKDIR:
        case SYSCALL_UNLINK:
        case SYSCALL_STAT:
        case SYSCALL_BRK:
        case SYSCALL_SBRK:
        case SYSCALL_EXEC:
        case SYSCALL_KILL:
        default: 
            return_value = ENOSYS;
            break;
    }


    syscall_exit:
    asm volatile ("cli;"); // in case of scheduler race after setting inside_kernel=0
    current_thread->inside_kernel = 0;
    return return_value;
}
