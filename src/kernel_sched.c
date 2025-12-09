#include "include/kernel_semaphore.h"
#include "include/kernel_spinlock.h"
#include "include/kernel_sched.h"
#include "include/fs/fs.h"
#include "include/kernel.h"
#include "../libc/src/include/string.h"
#include "include/kernel_interrupts.h"
#include "include/mm/kernel_memory.h"
#include "include/elf.h"
#include "include/lowlevel.h"
#include "include/kernel_gdt_idt.h"
#include <stdalign.h>
#include <stddef.h>

// all static functions assume locked scheduler
// NEVER alter scheduler process lists with enabled interrupts on the same core, WILL DEADLOCK

// TODO: rewrite for later multicore support

spinlock_t scheduler_lock = {0};

// TODO: This scheduler implementation assumes ALL kernel stacks are inside kernel's address space, redo to not be so
// this also means that any and all ring 0 threads'/processes' stacks HAVE to be mapped into the kernel's address space
// because they do not jump into a special kernel stack, context would end up somewhere where we don't have access after
// a address space change
// TODO: Check whether scheduler works for ring 1 and 2

// TODO: Completely rewrite signals

//#define kprintf(fmt, ...) kprintf("Scheduler: "fmt, ##__VA_ARGS__)

void reschedule() {
    asm volatile(
        "int %0;"
        ::"i"(PIC_INTERR_PIT + IDT_PIC_INTERR_START)
    );
}

__attribute__((naked)) void kernel_idle() { // in case all processes/threads are sleeping
    asm volatile (
        "loop:\n\t"
        "hlt; jmp loop"
    );
}

// process_list->prev = last item in the (doubly) linked list, last item has ->next NULL so we know when we reach the end
process_t * process_list = NULL;
process_t * kernel_task = NULL;
process_t * current_process = NULL;
thread_t * current_thread = NULL;

process_t * idle_task = NULL;

size_t last_pid = 0;

static inline void scheduler_init_idle_task() {
    idle_task = kalloc(sizeof(process_t));
    if (!idle_task) panic("Not enough memory for kernel idle task!\n");
    memset(idle_task, 0, sizeof(process_t));

    idle_task->pid = -1;
    idle_task->ring = 0;
    idle_task->address_space_vaddr = kernel_address_space_vaddr; 

    idle_task->threads = kalloc(sizeof(thread_t));
    if (!idle_task->threads) panic("Not enough memory for kernel idle task!\n");
    memset(idle_task->threads, 0, sizeof(thread_t));
    // don't write here anyway so this should be safe
    idle_task->threads->kernel_stack = kernel_ts_stack_top;
    idle_task->threads->context.esp = idle_task->threads->context.ebp = kernel_ts_stack_top;

    idle_task->threads->context.iret_frame.ip = kernel_idle;
    idle_task->threads->context.iret_frame.sp = kernel_ts_stack_top;
    idle_task->threads->context.iret_frame.flags = IA_32_EFL_SYSTEM_INTER_EN | IA_32_EFL_ALWAYS_1;
    idle_task->threads->context.iret_frame.cs = GDT_KERNEL_CODE << 3;
    idle_task->threads->context.iret_frame.ss = GDT_KERNEL_DATA << 3;

    idle_task->threads->prev = idle_task->threads->prev;
}

void print_registers(const context_t * context) {
    kprintf("eax %x\nebx %x\necx %x\nedx %x\nedi %x\nesi %x\n", context->eax, context->ebx, context->ecx, context->edx, context->edi, context->esi);
    kprintf("esp (gregs) %x\nesp (iret) %x\nebp %x\neip %x\nefl %x\n", context->esp, context->iret_frame.sp, context->ebp, context->iret_frame.ip, context->iret_frame.flags);
    kprintf("ss %x\ncs %x\n", context->iret_frame.ss, context->iret_frame.cs);
}

static inline void scheduler_print_processes() {
    process_t * printed = process_list;
    thread_t * printed_thread;
    while (printed != NULL) {
        printed_thread = printed->threads;
        while (printed_thread != NULL) {
            kprintf("pid %8d, tid %d, cr3 %x, eip %x, kernel esp %x, process esp %x, state %d, command %s\n", 
            printed->pid, printed_thread->tid, printed->address_space_vaddr, printed_thread->context.iret_frame.ip, printed_thread->context.esp, printed_thread->context.iret_frame.sp, printed_thread->status,
            printed->argc > 0?(printed->argv != NULL ? (printed->argv[0] != NULL ? printed->argv[0]:"(nil)"):"(nil)"):"(nil)");

            printed_thread = printed_thread->next;
        }
        printed = printed->next;
    }
}

void scheduler_print_process(const process_t * process) {
    kprintf("\
Prog:\t%s\n\
PID:\t%d\n\
PPID:\t%d\n\
CPL:\t%d\n\
UID:\t%d\n\
GID:\t%d\n\
Sigs:\t%x\n\
TID:\t%d\n",
//Saved Context:\n",
    (process->argv!=NULL?(process->argv[0] != NULL?process->argv[0]:"(nil)"):"(nil)"), process->pid, process->ppid, process->ring, process->uid,
    process->gid, process->signal, current_thread->tid);
    //print_registers(&process->context);
}

static char registering_kernel_task = 0;
static inline void scheduler_init_kernel_task() {
    registering_kernel_task = 1;
    reschedule(); // fastest way to correctly enter in the kernel task is to let the interrupt gather info
}

void scheduler_init() {
    scheduler_init_idle_task();
    scheduler_init_kernel_task();
    kprintf("Initialized\n");
}

static inline void push_process_to_end(process_t * process) {
    if (process->next == NULL) return; // already at the end

    if (process == process_list) {
        // process->next->prev is already valid (process)
        // process->prev is also already valid (previous last process)

        process_list = process->next;

        process->prev->next = process;

        process->next = NULL;
        return;
    }

    // unlink from queue
    process->next->prev = process->prev;
    process->prev->next = process->next;

    // link at the end
    process_list->prev->next = process;
    process->prev = process_list->prev;

    // restore metadata
    process_list->prev = process;
    process->next = NULL;
}

static inline void push_thread_to_end(process_t * pprocess, thread_t * thread) {
    if (thread->next == NULL) return; // already at the end

    if (thread == pprocess->threads) {
        // process->next->prev is already valid (process)
        // process->prev is also already valid (previous last process)

        pprocess->threads = thread->next;

        thread->prev->next = thread;

        thread->next = NULL;
        return;
    }

    // unlink from queue
    thread->next->prev = thread->prev;
    thread->prev->next = thread->next;

    // link at the end
    pprocess->threads->prev->next = thread;
    thread->prev = pprocess->threads->prev;

    // restore metadata
    pprocess->threads->prev = thread;
    thread->next = NULL;
}


void scheduler_add_process(struct program program, uint8_t ring) {
    if (process_list == NULL) panic("Called scheduler_add_process() before scheduler was initialized!");

    process_t * process = kalloc(sizeof(process_t));
    if (process == NULL) panic("Failed to allocate memory for process struct\n");
    memset(process, 0, sizeof(process_t));

    process->threads = kalloc(sizeof(thread_t));
    if (process->threads == NULL) panic("Failed to allocate memory for thread struct");
    memset(process->threads, 0, sizeof(thread_t));

    process->ring = ring;
    process->pid = ++last_pid;

    process->ppid = current_process->pid;
    process->pgrp = current_process->pgrp;
    process->session = current_process->session;

    process->address_space_vaddr = program.pd_vaddr;
    process->threads->kernel_stack = program.kernel_stack;
    process->threads->kernel_stack_size = program.kernel_stack_size;
    process->threads->cr3_state = paging_virt_addr_to_phys(process->address_space_vaddr);

    process->thread_stacks[0] = 1; // the base program stack

    process->threads->context.esp = process->threads->kernel_stack; // ESP will is the interrupt value, iret_frame.sp is the program stack
    process->threads->context.iret_frame.sp = program.stack;
    process->threads->status = SCHED_RUNNABLE;
    
    process->threads->context.iret_frame.ip = program.start;
    
    process->threads->context.iret_frame.flags = IA_32_EFL_SYSTEM_INTER_EN | IA_32_EFL_ALWAYS_1;
    
    if (ring == 3) {
        process->threads->context.iret_frame.cs = (GDT_USER_CODE << 3) | 3;
        process->threads->context.iret_frame.ss = (GDT_USER_DATA << 3) | 3;
    } else {
        process->threads->context.iret_frame.cs = GDT_KERNEL_CODE << 3;
        process->threads->context.iret_frame.ss = GDT_KERNEL_DATA << 3;
    }

    process->threads->prev = process->threads;


    unsigned long prev_eflags;
    asm volatile ("pushf; pop %0;" : "=R"(prev_eflags));
    asm volatile("cli"); // if we leave interrupts on, a PIT timer tick could lead to the scheduler deadlocking itself

    spinlock_acquire(&scheduler_lock);
    process_list->prev->next = process;
    process->prev = process_list->prev;
    process_list->prev = process;
    spinlock_release(&scheduler_lock);

    asm volatile ("push %0; popf;" :: "R"(prev_eflags));

    reschedule();
}

#define KERNEL_ARGV0 "kernel/core"
static inline void register_kernel_task(context_t * context) {
    kernel_task = kalloc(sizeof(process_t));
    if (!kernel_task) panic("Not enough memory for kernel task!");
    memset(kernel_task, 0, sizeof(process_t));

    kernel_task->threads = kalloc(sizeof(thread_t));
    if (!kernel_task) panic("Not enough memory for kernel task!");
    memset(kernel_task->threads, 0, sizeof(thread_t));
    kernel_task->threads->prev = kernel_task->threads;

    memcpy(&kernel_task->threads->context, context, sizeof(context_t) - 2*sizeof(void*)); // kernel is ring 0 and when switching from ring 0 (interrupt) to ring 0, SS and SP are not pushed

    kernel_task->threads->context.iret_frame.ss = GDT_KERNEL_DATA << 3; // just so we have correct information here
    kernel_task->threads->context.iret_frame.sp = (void*)kernel_task->threads->context.esp;

    kernel_task->threads->kernel_stack = kernel_ts_stack_top;
    kernel_task->threads->kernel_stack_size = KERNEL_TS_STACK_SIZE;

    kernel_task->address_space_vaddr = kernel_address_space_vaddr;
    kernel_task->threads->cr3_state = paging_virt_addr_to_phys(kernel_task->address_space_vaddr);

    kernel_task->threads->status = SCHED_RUNNING;

    kernel_task->argc = 1;
    kernel_task->argv = kalloc(sizeof(char*));
    kernel_task->argv[0] = KERNEL_ARGV0;

    if (process_list == NULL) {
        process_list = kernel_task;
        process_list->prev = kernel_task;
    } else {
        panic("Existing processes while registering kernel process");
        //process_list->prev->next = kernel_task;
        //kernel_task->prev = process_list->prev;
        //process_list->prev = kernel_task;
    }


    kernel_task->fds[0] = kalloc(sizeof(file_descriptor_t));
    kernel_task->fds[1] = kalloc(sizeof(file_descriptor_t));
    kernel_task->fds[2] = kalloc(sizeof(file_descriptor_t));
    
    if (kernel_task->fds[0] == NULL || kernel_task->fds[1] == NULL || kernel_task->fds[2] == NULL) panic("No memory free to allocate kernel's file descriptors");

    memset(kernel_task->fds[0], 0, sizeof(file_descriptor_t));
    memset(kernel_task->fds[1], 0, sizeof(file_descriptor_t));
    memset(kernel_task->fds[2], 0, sizeof(file_descriptor_t));

    current_process = kernel_task;
    current_thread = kernel_task->threads;
}


static inline void scheduler_remove_process(process_t * process) {
    while (process->threads != NULL) {
        kernel_destroy_thread(process, process->threads);
    }

    paging_destroy_address_space(current_process->address_space_vaddr);

    if (process->next != NULL) process->next->prev = process->prev;
    else process_list->prev = process->prev; // is at the end process->prev->next handled by next line

    if (process != process_list) process->prev->next = process->next;
    else process_list = process->next; // metadata fixed in process->next != NULL
    
    kfree(process);
}

static void inline switch_context(process_t * pprocess, thread_t * thread, context_t * context) {
    tss_set_stack(thread->kernel_stack);
    
    if (paging_get_address_space_paddr() != thread->cr3_state)  // to prevent TLB flush performance hit
        paging_apply_address_space(thread->cr3_state);

    // iret requires ESP and SS when returning to a different (less) privileged CPL, however not when returning to 0

    if (pprocess->ring == 0 || thread->inside_kernel) {
        memcpy(thread->context.esp, &thread->context.iret_frame, sizeof(struct interr_frame) - 2 * sizeof(void *)); // ring 0 already contains the iret frame from last interrupt since it did not switch stacks
    } else {
        thread->context.esp -= sizeof(struct interr_frame);
        memcpy(thread->context.esp, &thread->context.iret_frame, sizeof(struct interr_frame));
    }

    memcpy(context, &thread->context, sizeof(context_t)-sizeof(struct interr_frame));

    if (pprocess->ring == 0 || thread->inside_kernel) thread->context.esp += sizeof(struct interr_frame);


    unsigned int data_segment = thread->inside_kernel ? (GDT_KERNEL_DATA << 3) : (thread->context.iret_frame.ss);

    asm volatile (
        "movl %0, %%eax;"
        "movl %%eax, %%ds;"
        "movl %%eax, %%es;"
        "movl %%eax, %%fs;"
        "movl %%eax, %%gs;"
        ::"m"(data_segment):"eax"
    );
}

void signal_process_group(pid_t process_group, unsigned short signal) {
    if (signal >= __sig_last) return; // not a valid signal
    spinlock_acquire(&scheduler_lock);
    process_t * signaled = process_list;
    thread_t * signaled_thread = NULL;
    while (signaled != NULL) {
        if (signaled->pgrp == process_group) {
            signaled->signal |= GET_SIG_MASK(signal);
            if (after_signal_states[signal] == SCHED_STOPPED) {
                signaled->is_stopped = 1;
                continue;
            }
            else if (after_signal_states[signal] == SCHED_RUNNABLE) {
                signaled->is_stopped = 0;
                continue;
            }

            signaled_thread = signaled->threads;
            while (signaled_thread != NULL) {
                signaled_thread->status = after_signal_states[signal];
                signaled_thread = signaled_thread->next;
            }
        }
        signaled = signaled->next;
    }

    spinlock_release(&scheduler_lock);
    asm volatile("sti;");
    reschedule();
}

void schedule(context_t * context) {
    spinlock_acquire(&scheduler_lock);

    if (current_thread != NULL) memcpy(&current_thread->context, context, sizeof(struct context_t) - ((current_process->ring == 0 || current_thread->inside_kernel) ? 2*sizeof(void *) : 0)); // ring 3 -> ring 0 causes SS and SP to be pushed
    if (current_thread != NULL) current_thread->cr3_state = paging_get_address_space_paddr();
    //tss_set_stack(kernel_ts_stack_top); shouldn't be needed


    if (__builtin_expect(registering_kernel_task, 0)) {
        registering_kernel_task = 0;
        register_kernel_task(context);
        kprintf("registered kernel task\n");
    }

    if (__builtin_expect(process_list == NULL, 0)) { // should always at least contain the kernel task (set up as above), so this would be before scheduler init
        spinlock_release(&scheduler_lock);
        return;
    };


    if (current_process != NULL)
        push_process_to_end(current_process);

    if (current_process != NULL && current_thread != NULL && current_thread->status == SCHED_RUNNING) {
        current_thread->status = SCHED_RUNNABLE;
        push_thread_to_end(current_process, current_thread);
    } // could've been set (eg by a syscall) to uninterruptable
    
    //scheduler_print_processes();

    scheduler_start:
    current_process = process_list; // halts here with KVM and -cpu host on AMD Ryzen 7 5700, no clue why

    while (current_process != NULL) {
        current_thread = current_process->threads;

        while (current_thread != NULL) {
            switch (current_thread->status) {
                case SCHED_RUNNING:
                    break;
                case SCHED_RUNNABLE:
                    current_thread->status = SCHED_RUNNING;
                    push_process_to_end(current_process);
                    push_thread_to_end(current_process, current_thread);
                                    
                    switch_task:
                    switch_context(current_process, current_thread, context);
                    //kprintf("Exiting scheduler with pid %d tid %d eip %x\n", current_process->pid, current_thread->tid, current_thread->context.iret_frame.ip);
                    spinlock_release(&scheduler_lock);
                    return;
                case SCHED_STOPPED:
                case SCHED_INTERR_SLEEP:
                case SCHED_UNINTERR_SLEEP:
                case SCHED_ZOMBIE:
                    break;
                case SCHED_THREAD_CLEANUP:
                    kernel_destroy_thread(current_process, current_thread);
                    if (current_process->threads != NULL) {
                        goto scheduler_start;
                        break;
                    }
                case SCHED_CLEANUP: // this means that threads can still run a while before the one calling exit() gets scheduled
                    if (current_process->pid == 0) {
                        panic("Tried to kill kernel");
                    } else if (current_process->pid == 1) {
                        panic("Tried to kill init");
                    }
                    scheduler_remove_process(current_process); // aka any thread can terminate the whole process
                    scheduler_print_processes();
                    goto scheduler_start; // internal reschedule()
            }
            current_thread = current_thread->next;
        }
        current_process = current_process->next;
    }
    current_process = idle_task;
    current_thread = idle_task->threads;
    kprintf("Entered idle task\n");
    goto switch_task;
}