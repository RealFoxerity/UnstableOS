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
#include "include/vga.h"
#include <stddef.h>

// all static functions assume locked scheduler
// NEVER alter scheduler process lists with enabled interrupts on the same core, WILL DEADLOCK

// TODO: rewrite for later multicore support

spinlock_t scheduler_lock = {0};

// TODO: This scheduler implementation assumes ALL kernel stacks are inside kernel's address space, redo to not be so
// if fixing stacks, fix kernel_exec.c
// this also means that any and all ring 0 threads'/processes' stacks HAVE to be mapped into the kernel's address space
// because they do not jump into a special kernel stack, context would end up somewhere where we don't have access after
// a address space change
// TODO: Check whether scheduler works for ring 1 and 2

// TODO: Completely rewrite signals

//#define kprintf(fmt, ...) kprintf("Scheduler: "fmt, ##__VA_ARGS__)

__attribute__((naked)) void reschedule() {
    asm volatile (
        "pushf;"
        "sti;"
        "int $0x20;"
        "popf;"
        "ret;"
    );
}

__attribute__((naked)) void kernel_idle() { // loop to jump to when a thread is supposed to end
    asm volatile (
        "loop:\n\t"
        "hlt; jmp loop"
    );
}

// process_list->prev = last item in the (doubly) linked list, last item has ->next NULL so we know when we reach the end
process_t * process_list = NULL;
process_t * zombie_list = NULL;

process_t * kernel_task = NULL;
process_t * current_process = NULL;
thread_t * current_thread = NULL;

pid_t last_pid = 0;

void print_registers(const mcontext_t * context) {
    kprintf("eax %lx\nebx %lx\necx %lx\nedx %lx\nedi %lx\nesi %lx\n", context->eax, context->ebx, context->ecx, context->edx, context->edi, context->esi);
    kprintf("esp (gregs) %p\nesp (iret) %p\nebp %p\neip %p\nefl %lx\n", context->esp, context->iret_frame.sp, context->ebp, context->iret_frame.ip, context->iret_frame.flags);
    kprintf("ss %lx\ncs %lx\n", context->iret_frame.ss, context->iret_frame.cs);
}

void scheduler_print_processes() {
    process_t * printed = process_list;
    thread_t * printed_thread;
    while (printed != NULL) {
        printed_thread = printed->threads;
        while (printed_thread != NULL) {
            kprintf("pid %8lu, tid %lu, cr3 %p, eip %p, kernel esp %p, process esp %p, state %d, command %s\n",
            printed->pid, printed_thread->tid, printed->address_space_paddr, printed_thread->context.iret_frame.ip, printed_thread->context.esp, printed_thread->context.iret_frame.sp, printed_thread->status,
            printed->argc > 0?(printed->argv != NULL ? (printed->argv[0] != NULL ? printed->argv[0]:"(nil)"):"(nil)"):"(nil)");

            printed_thread = printed_thread->next;
        }
        printed = printed->next;
    }
}

void scheduler_print_process(const process_t * process) {
    kprintf("\
Prog:\t%s\n\
PID :\t%lu\n\
PPID:\t%lu\n\
CPL :\t%d\n\
UID :\t%lu\n\
GID :\t%lu\n\
PSig:\t%llx\n\
TID :\t%lu\n",
//Saved Context:\n",
    (process->argv!=NULL?(process->argv[0] != NULL?process->argv[0]:"(nil)"):"(nil)"), process->pid, process->parent->pid, process->ring, process->uid,
    process->gid, (unsigned long long)process->sa_pending, current_thread->tid);
    //print_registers(&process->context);
}

static char registering_kernel_task = 0;
static inline void scheduler_init_kernel_task() {
    registering_kernel_task = 1;
    reschedule(); // fastest way to correctly enter in the kernel task is to let the interrupt gather info
}

void scheduler_init() {
    scheduler_init_kernel_task();
}

static inline void push_process_to_end(process_t * process) {
    if (process->next == NULL) return; // already at the end

    UNLINK_DOUBLE_LINKED_LIST(process, process_list)
    APPEND_DOUBLE_LINKED_LIST(process, process_list);
}

static inline void push_thread_to_end(process_t * pprocess, thread_t * thread) {
    if (thread->next == NULL) return; // already at the end

    UNLINK_DOUBLE_LINKED_LIST(thread, pprocess->threads)
    APPEND_DOUBLE_LINKED_LIST(thread, pprocess->threads);
}

#define KERNEL_ARGV0 "kernel/core"
static inline void register_kernel_task(mcontext_t * context) {
    kernel_task = kalloc(sizeof(process_t));
    if (!kernel_task) panic("Not enough memory for kernel task!");
    memset(kernel_task, 0, sizeof(process_t));

    kernel_task->parent = kernel_task;

    kernel_task->threads = kalloc(sizeof(thread_t));
    if (!kernel_task) panic("Not enough memory for kernel task!");
    memset(kernel_task->threads, 0, sizeof(thread_t));
    kernel_task->threads->instances = 1;

    kernel_task->threads->prev = kernel_task->threads;

    memcpy(&kernel_task->threads->context, context, sizeof(mcontext_t) - 2*sizeof(void*)); // kernel is ring 0 and when switching from ring 0 (interrupt) to ring 0, SS and SP are not pushed

    kernel_task->threads->context.iret_frame.ss = GDT_KERNEL_DATA << 3; // just so we have correct information here
    kernel_task->threads->context.iret_frame.sp = (void*)kernel_task->threads->context.esp;

    kernel_task->threads->kernel_stack = kernel_ts_stack_top;
    kernel_task->threads->kernel_stack_size = KERNEL_TS_STACK_SIZE;

    kernel_task->address_space_paddr = paging_virt_addr_to_phys(KERNEL_ADDRESS_SPACE_VADDR);
    kernel_task->threads->cr3_state = kernel_task->address_space_paddr;

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

    current_process = kernel_task;
    current_thread = kernel_task->threads;
}


static inline char scheduler_remove_process(process_t * process) {
    for (thread_t * thread = process->threads; thread != NULL; ) {
        if (!kernel_destroy_thread(process, thread)) {
            thread = thread->next;
        } else {
            thread = process->threads;
        }
    }
    if (process->threads != NULL) return 0;

    paging_apply_address_space(paging_virt_addr_to_phys(KERNEL_ADDRESS_SPACE_VADDR));

    PAGE_DIRECTORY_TYPE * mapped_as = paging_map_phys_addr_unspecified(process->address_space_paddr, PTE_PDE_PAGE_WRITABLE);
    paging_destroy_address_space(mapped_as);
    paging_unmap_page(mapped_as);

    for (int i = 0; i < FD_LIMIT_PROCESS; i++) {
        if (process->fds[i]) {
            close_file(process->fds[i]);
        }
    }

    for (int i = 0; i < SEM_NSEMS_MAX; i++) {
        if (process->semaphores[i] == NULL) continue;
        if (__atomic_sub_fetch(&process->semaphores[i]->used, 1, __ATOMIC_RELAXED) == 0)
            kfree(process->semaphores[i]);
    }

    UNLINK_DOUBLE_LINKED_LIST(process, process_list)

    //kfree(process);
    // not freed because we want to form a zombie queue for wait()
    return 1;
}

static void inline switch_context(process_t * pprocess, thread_t * thread, mcontext_t * context) {
    tss_set_stack(thread->kernel_stack);

    if (paging_get_address_space_paddr() != thread->cr3_state)  // to prevent TLB flush performance hit
        paging_apply_address_space(thread->cr3_state);

    // iret requires ESP and SS when returning to a different (less) privileged CPL, however not when returning to 0
    // we are copying into the target processes stack because we need to pop esp even when not switching privilage levels
    if ((thread->context.iret_frame.cs & ~3) == GDT_KERNEL_CODE << 3) { // ring 0 code segment
        memcpy(thread->context.esp, &thread->context.iret_frame, sizeof(struct interr_frame) - 2 * sizeof(void *)); // ring 0 already contains the iret frame from last interrupt since it did not switch stacks (to tss kernel stack)
    } else {
        thread->context.esp -= sizeof(struct interr_frame); // consequently, ring 0+ doesn't have anything there so we need to make room
        memcpy(thread->context.esp, &thread->context.iret_frame, sizeof(struct interr_frame));
    }

    memcpy(context, &thread->context, sizeof(mcontext_t)-sizeof(struct interr_frame));

    //unsigned int data_segment = (thread->context.iret_frame.cs & ~3) == (GDT_KERNEL_CODE << 3) ? (GDT_KERNEL_DATA << 3) : (thread->context.iret_frame.ss);
    // this top one is the correct approach, but when testing KVM and real hardware, i always had issues with
    // the segment selectors getting set incorrectly to kernel selectors which would zero them out when
    // context switching into userspace raising #GP. this bottom approach fixes this issue, but is technically
    // wrong. because we don't actually do segmentation and everything is protected via paging, it doesn't actually
    // matter what segments we use for data at what ring (except the stack which is set based on the cs)

    unsigned int data_segment = (GDT_USER_DATA << 3) | 3;
    asm volatile (
        "movl %0, %%eax;"
        "movl %%eax, %%ds;"
        "movl %%eax, %%es;"
        "movl %%eax, %%fs;"
        "movl %%eax, %%gs;"
        ::"m"(data_segment):"eax"
    );
}

void schedule(mcontext_t * context) {
    if (scheduler_lock.state == SPINLOCK_LOCKED) return;
    spinlock_acquire_nonreentrant(&scheduler_lock);
    if (__builtin_expect(registering_kernel_task, 0)) {
        registering_kernel_task = 0;
        register_kernel_task(context);
    }

    if (__builtin_expect(process_list == NULL, 0)) { // should always at least contain the kernel task (set up as above), so this would be before scheduler init
        spinlock_release(&scheduler_lock);
        return;
    };


    if (current_thread != NULL) memcpy(&current_thread->context, context, sizeof(mcontext_t) - (((context->iret_frame.cs & ~3) == GDT_KERNEL_CODE << 3) ? 2*sizeof(void *) : 0)); // ring 3 -> ring 0 causes SS and SP to be pushed
    if (current_thread != NULL) current_thread->cr3_state = paging_get_address_space_paddr();
    //tss_set_stack(kernel_ts_stack_top); shouldn't be needed

    if (current_process != NULL)
        push_process_to_end(current_process);

    if (current_process != NULL && current_thread != NULL && current_thread->status == SCHED_RUNNING) {
        current_thread->status = SCHED_RUNNABLE;
        push_thread_to_end(current_process, current_thread);
    } // status could've been set (eg by a syscall) to uninterruptable

    //scheduler_print_processes();

    scheduler_start:
    for (process_t * checked_process = process_list; checked_process != NULL; checked_process = checked_process->next) {
        if (checked_process->do_cleanup || checked_process->threads == NULL) {
            cleanup_process:
            if (checked_process->pid == 0) {
                panic("Tried to kill kernel");
            } else if (checked_process->pid == 1) {
                panic("Tried to kill init");
            }
            if (!scheduler_remove_process(checked_process))
                goto partial_cleanup;

            // reparent all child processes if any
            process_t * checked = process_list;
            process_t * parent = checked_process->parent;
            if (parent == NULL) panic("Corrupt process structure (parent is NULL)!");

            while (checked != NULL) {
                if (checked->parent == checked_process)
                    checked->parent = checked_process->parent;
                checked = checked->next;
            }
            // cleanup zombie list
            checked = zombie_list;
            while (checked != NULL) {
                if (checked->parent == checked_process) {
                    UNLINK_DOUBLE_LINKED_LIST(checked, zombie_list)

                    checked = checked->next;
                    kfree(checked->prev); // avoid uaf
                    continue;
                }
                checked = checked->next;
            }

            if (checked_process->sa_handlers[SIGCHLD - 1].sa_handler == SIG_IGN ||
                checked_process->sa_handlers[SIGCHLD - 1].sa_flags & SA_NOCLDWAIT) {
                    kfree(checked_process);
                    goto scheduler_start;
                }

            APPEND_DOUBLE_LINKED_LIST(checked_process, zombie_list)

            // wake up parent process
            thread_t * checked_thread = parent->threads;
            while (checked_thread != NULL) {
                if (checked_thread->status == SCHED_WAITING) {
                    checked_thread->status =  SCHED_RUNNABLE;
                    break;
                }
                checked_thread = checked_thread->next;
            }
            goto scheduler_start; // internal reschedule()
        }
        signal_retry_process(checked_process);

        partial_cleanup:
        for (thread_t * checked_thread = checked_process->threads; checked_thread != NULL; checked_thread = checked_thread->next) {
            if (checked_thread->instances == 0) panic("Encountered thread with instances 0, UAF?");
            switch (checked_thread->status) {
                case SCHED_RUNNING:
                    break;
                case SCHED_RUNNABLE:
                    if (checked_process->is_stopped &&
                        !checked_thread->in_critical_section)
                            continue;
                    checked_thread->status = SCHED_RUNNING;
                    push_process_to_end(checked_process);
                    push_thread_to_end(checked_process, checked_thread);

                    switch_context(checked_process, checked_thread, context);
                    // we need the correct address space
                    if (checked_thread->context.iret_frame.cs & 3) {
                        signal_dispatch_sa(checked_process, checked_thread);
                    }
                    spinlock_release(&scheduler_lock);
                    current_process = checked_process;
                    current_thread  = checked_thread;
                    return;
                case SCHED_INTERR_SLEEP:
                case SCHED_UNINTERR_SLEEP:
                case SCHED_WAITING:
                    break;
                case SCHED_THREAD_CLEANUP:
                    if (checked_thread->in_critical_section)
                        panic("Thread marked for cleanup in critical section, corrupted process list?");
                    kernel_destroy_thread(checked_process, checked_thread);
                    if (checked_process->threads == NULL) goto cleanup_process;
                    goto scheduler_start;
            }
        }
    }
    panic("No viable task exists, corrupted process list?");
    __builtin_unreachable();
}