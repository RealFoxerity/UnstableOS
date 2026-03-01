#include <stdint.h>

#include "include/kernel_exec.h"
#include "include/kernel_interrupts.h"
#include "include/kernel.h"
#include "../libc/src/include/string.h"
#include "include/mm/kernel_memory.h"
#include "include/vga.h"
#include "include/errno.h"
#include "include/kernel_sched.h"
#include "include/kernel_semaphore.h"
#include "include/fs/fs.h"

#include "include/kernel_gdt_idt.h"

#define kprintf(fmt, ...) kprintf("Kernel Routines: "fmt, ##__VA_ARGS__)

extern void clear_screen_fatal(); // kernel_interrupts.c

extern uint8_t vga_x, vga_y;
#define ssize_t long

static char check_address_range(const void * addr, size_t n, char writable, char in_kernel) { // check whether the address range is inside the program, is mapped, and whether it is writable (assumes correct address space)
    if (addr == NULL) return 0;
    n += (unsigned long)addr & (PAGE_SIZE_NO_PAE - 1);
    addr = (void*)((unsigned long) addr & ~(PAGE_SIZE_NO_PAE - 1));

    for (const void * iteraddr = addr; iteraddr < addr+n && iteraddr >= addr; iteraddr += PAGE_SIZE_NO_PAE) { // > addr in case we wrap around
        PAGE_TABLE_TYPE pte = paging_get_pte(iteraddr);
        if (pte == 0) {
            if (!(iteraddr >= PROGRAM_HEAP_VADDR && iteraddr < PROGRAM_HEAP_VADDR + PROGRAM_HEAP_SIZE))
                return 0;
            else {
                // overcommitment, we could rely on page faults, but that would
                // require all syscalls to have interrupts enabled at all times
                paging_add_page((void *)iteraddr, PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE);
                continue;
            }
        }

        if ((PAGE_DIRECTORY_TYPE*)iteraddr >= PTE_ADDR_VIRT_BASE) return 0; // even though this is theoretically a valid kernel operation, probably not intended
        if (!in_kernel) {
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

long kernel_syscall_dispatcher(context_t ctx);
// since we use system V abi, arg4 is pushed onto the stack by the user
__attribute__((naked, no_caller_saved_registers)) void interr_syscall(struct interr_frame * interrupt_frame) {
    asm volatile (
        "pusha;"
        "call kernel_syscall_dispatcher;"
        "addl $0x20, %esp;" // 8 registers from pusha
        "iret;"
    );
}

long kernel_syscall_dispatcher(context_t ctx) {
    enum syscalls syscall_number = ctx.eax;
    long 
    arg1 = ctx.edi,
    arg2 = ctx.esi,
    arg3 = ctx.edx,
    arg4 = ((long*)(ctx.iret_frame.sp))[3]; // no clue why [3], but it actually is

    long return_value = ENOSYS;

    kassert(current_process);
    kassert(current_thread);

    // we might want to call syscalls from other syscalls and/or drivers
    char in_kernel = (ctx.iret_frame.cs & 3) == 0;

    switch (syscall_number) {
        case SYSCALL_YIELD:
            reschedule();
            break;
        case SYSCALL_CREATE_THREAD:
            kernel_create_thread(current_process, (void*)arg1, (void*)arg2); // theoretically don't have to check bounds since they would just cause a segmentation fault
            break;
        case SYSCALL_EXIT_THREAD: // returns exitcode
            current_thread->status = SCHED_THREAD_CLEANUP;
            reschedule();
            asm volatile ("jmp kernel_idle");
            break;

        case SYSCALL_EXIT:
            current_process->exitcode = arg1;
        case SYSCALL_ABORT:
            if (syscall_number == SYSCALL_ABORT) kprintf("Thread %lu of process %lu called abort()!\n", current_thread->tid, current_process->pid); // so that we can keep the fall-through for syscall_exit
            current_thread->status = SCHED_CLEANUP;
            reschedule();
            asm volatile ("jmp kernel_idle");
            break;
        
        case SYSCALL_WRITE:
            if (!check_address_range((const void*)arg2, arg3, 0, in_kernel)) {
                return_value = EFAULT;
                break;
            }
            asm volatile ("sti;");
            return_value =  sys_write(arg1, (const void*)arg2, arg3);
            break;
        case SYSCALL_READ:
            if (!check_address_range((void*)arg2, arg3, 1, in_kernel)) {
                return_value = EFAULT;
                break;
            }
            asm volatile ("sti;");
            return_value = sys_read(arg1, (void*)arg2, arg3);
            break;
        case SYSCALL_DUP:
            asm volatile ("sti;");
            sys_dup(arg1);
            break;
        case SYSCALL_DUP2:
            asm volatile ("sti;");
            sys_dup2(arg1, arg2);
            break;
        case SYSCALL_SEEK:
            asm volatile ("sti;");
            return_value = sys_seek(arg1, arg2, arg3);
            break;
        case SYSCALL_INTERR_RING2_PANIC:
            if ((ctx.iret_frame.cs & ~3) == GDT_USER_CODE << 3) break; // has to be at least ring 2 or has to be called within a kernel routine
        
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
            asm volatile ("sti;");

            kernel_sem_post(current_process, arg1);
            break;
        case SYSCALL_SEM_WAIT:
            if (arg1 < 0 || arg1 >= PROGRAM_MAX_SEMAPHORES) {
                return_value = EINVAL;
                break;
            }
            if (!current_process->semaphores[arg1].used) {
                kprintf("Called sem_wait on invalid (unused) semaphore (%lu)\n", arg1);

                return_value = EINVAL;
                break;
            }
            asm volatile ("sti;");

            kernel_sem_wait(current_process, current_thread, arg1);
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
            return_value = sys_getpgid((pid_t)arg1);
            break;
        case SYSCALL_MOUNT:
            if (!check_address_range((const void*)arg1, 1, 0, in_kernel)) {
                return_value = EFAULT;
                break;
            }
            return_value = sys_mount((const char*)arg1, (const char*)arg2, (unsigned char)arg3, (unsigned short)arg4);
            break;
        case SYSCALL_OPEN:
            asm volatile ("sti;");
            // it is up to sys_open to securely copy the path (arg1)
            return_value = sys_open((const char *)arg1, arg2, arg3);
            break;
        case SYSCALL_CLOSE:
            asm volatile ("sti;");
            return_value = sys_close(arg1);
            break;
        case SYSCALL_EXEC:
            asm volatile ("sti;");
            return_value = sys_exec((const char *)arg1);
            break;
        case SYSCALL_SPAWN:
            asm volatile ("sti;");
            return_value = sys_spawn((const char *)arg1);
            break;
        case SYSCALL_FORK:
            return_value = sys_fork(&ctx);
            break;
        case SYSCALL_WAIT:
            if (arg1 != 0) {
                if (!check_address_range((void*)arg1, sizeof(int), 1, in_kernel)) {
                    return_value = EFAULT;
                    break;
                }
            }
            return_value = sys_wait((int*)arg1);
            break;
        case SYSCALL_SETPGID:
        case SYSCALL_TCGETPGRP:
        case SYSCALL_TCSETPGRP:
        case SYSCALL_MKDIR:
        case SYSCALL_UNLINK:
        case SYSCALL_STAT:
        case SYSCALL_BRK:
        case SYSCALL_SBRK:
        case SYSCALL_KILL:
        default: 
            return_value = ENOSYS;
            break;
    }


    syscall_exit:
    asm volatile ("cli;");

    // somehow reentrant syscalls break segment selectors upon exit, TODO: figure out why?
    asm volatile (
        "mov %0, %%ds;"
        "mov %0, %%es;"
        "mov %0, %%fs;"
        "mov %0, %%gs;"
        ::"R"(current_process->ring > 0 ? ((GDT_USER_DATA<<3) | 3) : (GDT_KERNEL_DATA<<3))
    );
    return return_value;
}
