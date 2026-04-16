#include "kernel.h"
#include "lowlevel.h"
#include "kernel_gdt_idt.h"
#include "mm/kernel_memory.h"
#include "v8086.h"
#include <string.h>

#include "kernel_sched.h"

// TODO: we absolutely don't support REP, and A32 (0x67), the latter of which shouldn't be needed though
// TODO: we support only 1 task running at once! Stack should be already safe for multitasking
// TODO: i don't know what's supposed to happen on a stack overflow (underflow?)
// note: each task gets only 4KiB of stack mapped

static unsigned short * real_mode_ivt = (unsigned short *)0;
#define V86_STACK_END 0x7FFF0

#define V86_TERMINATING_INTERRUPT 3

// thread to be resumed upon task's termination
thread_t * waiting_thread = NULL;
v86_mcontext_t pending_context;

static PAGE_DIRECTORY_TYPE * v86_create_new_address_space() {
    PAGE_DIRECTORY_TYPE * new_pd = paging_virt_addr_to_phys(paging_create_new_address_space());
    PAGE_TABLE_TYPE     * new_pt = pfalloc();
    kassert(new_pd);
    kassert(new_pt);

    PAGE_DIRECTORY_TYPE * mapped_pd = paging_map_phys_addr_unspecified(new_pd, PTE_PDE_PAGE_WRITABLE);
    PAGE_TABLE_TYPE     * mapped_pt = paging_map_phys_addr_unspecified(new_pt, PTE_PDE_PAGE_WRITABLE);
    kassert(mapped_pd);
    kassert(mapped_pt);

    //memcpy(mapped_pd, PDE_ADDR_VIRT,      PAGE_DIRECTORY_ENTRIES * sizeof(PAGE_DIRECTORY_TYPE));
    memcpy(mapped_pt, PTE_ADDR_VIRT_BASE, PAGE_TABLE_ENTRIES     * sizeof(PAGE_TABLE_TYPE));

    // vm86 permanently runs in ring 3, so we need to remap the lower 1MiB with user access
    // 256 * 4KiB page size = 1MiB
    // writable should be set from the identity paging setup, but to be safe
    for (int i = 0; i < 256; i++) {
        mapped_pt[i] |= PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE;
    }

    // emulate the address wraparound by setting the next 64KiB
    for (int i = 0; i < 64/4; i++) {
        mapped_pt[256 + i] = mapped_pt[i];
    }

    mapped_pt[V86_STACK_END/PAGE_SIZE_NO_PAE] = (PAGE_TABLE_TYPE)pfalloc();
    kassert(mapped_pt[V86_STACK_END/PAGE_SIZE_NO_PAE]);
    mapped_pt[V86_STACK_END/PAGE_SIZE_NO_PAE] |= PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_WRITABLE | PTE_PDE_PAGE_USER_ACCESS;

    mapped_pd[0] = (PAGE_TABLE_TYPE)new_pt | PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE;

    paging_unmap_page(mapped_pd);
    paging_unmap_page(mapped_pt);
    return new_pd;
}

// TODO: don't statically memcpy areas to memory, make new pages, so that we can spawn multiple tasks
// and change segments accordingly
// TODO: allow code to be copied somewhere else than from 0x500
v86_mcontext_t run_v86_task(void * code_start, size_t code_size,  void * final_ip, v86_mcontext_t regs) {
    if (waiting_thread != NULL) panic("Tried to spawn a Virtual-8086 task while one is already running!");
    if (current_process != kernel_task) panic("Tried to spawn a Virtual-8086 task outside of the kernel task!");
    if (code_size > X86_SEGMENT_SIZE) panic("Tried to spawn a Virtual-8086 task with code larger than segment size!");

    thread_t * new_thread = kalloc(sizeof(thread_t));
    kassert(new_thread);
    memset(new_thread, 0, sizeof(thread_t));

    new_thread->instances = 1;
    new_thread->tid       = __atomic_add_fetch(&last_tid, 1, __ATOMIC_RELAXED);
    new_thread->status    = SCHED_RUNNABLE;

    new_thread->kernel_stack = kalloc(PROGRAM_KERNEL_STACK_SIZE) + PROGRAM_KERNEL_STACK_SIZE;
    kassert(new_thread->kernel_stack);
    new_thread->kernel_stack_size = PROGRAM_KERNEL_STACK_SIZE;
    new_thread->stack             = (void *)V86_STACK_END;
    new_thread->stack_size        = PAGE_SIZE_NO_PAE;

    new_thread->v86_context            = regs;
    new_thread->v86_context.esp = new_thread->kernel_stack - PROGRAM_KERNEL_STACK_SIZE;
    new_thread->v86_context.iret_frame = (struct v86_interr_frame)
    {
        .ip = (void*)V86_FAR_MAKE(X86_CONVENTIONAL_MEMORY_START >> 4, (unsigned long) final_ip),
        .sp = (void*)V86_FAR_MAKE(V86_FAR_SEG(V86_STACK_END), V86_FAR_OFF(V86_STACK_END)),
        .ss = V86_FAR_SEG(V86_STACK_END),
        .cs = X86_CONVENTIONAL_MEMORY_START >> 4,
        .ds = 0, // I decided that having the data segment at 0 will make getting variables out *slightly* easier
        .es = 0,
        .fs = 0,
        .gs = 0,
        .flags = IA_32_EFL_SYSTEM_VM8086 | IA_32_EFL_ALWAYS_1 | IA_32_EFL_SYSTEM_INTER_EN,
    };


    memcpy((void*)X86_CONVENTIONAL_MEMORY_START, code_start, code_size);

    PAGE_DIRECTORY_TYPE * new_as = v86_create_new_address_space();
    kassert(new_as);
    new_thread->cr3_state = new_as;

    spinlock_acquire(&scheduler_lock);

    waiting_thread = current_thread;
    current_thread->status = SCHED_UNINTERR_SLEEP;

    APPEND_DOUBLE_LINKED_LIST(new_thread, current_process->threads);
    spinlock_release(&scheduler_lock);

    reschedule();
    //kalloc_print_heap_objects();


    v86_mcontext_t ctx = pending_context; // avoid race with waiting_thread = NULL
    waiting_thread = NULL;
    return ctx;
}

/***************************
 * instructions we have to handle for the v86 task
 ***************************/
#define X86_O32   0x66 // operand prefix to bump up to 32 bits
//#define X86_A32   0x66 // operand prefix to bump up to 32 bits
// alternatively make a TSS with a IOPL bitmap allowing everything
#define X86_CLI   0xFA
#define X86_STI   0xFB

#define X86_PUSHF 0x9C
#define X86_POPF  0x9D

#define X86_INT3  0xCC
#define X86_INT   0xCD
#define X86_INTO  0xCE
#define X86_IRET  0xCF

// ******** IN instructions
// from IMM to A*
#define X86_IN_AL_IMM 0xE4 // byte
#define X86_IN_AX_IMM 0xE5 // word
// from DX to A*
#define X86_IN_AL_DX  0xEC // byte
#define X86_IN_AX_DX  0xED // word

// byte from IO port in DX into ES:DI
#define X86_INSB  0x6C
#define X86_INSW  0x6D

// ******** OUT instructions
#define X86_OUT_AL_IMM 0xE6
#define X86_OUT_AX_IMM 0xE7

#define X86_OUT_AL_DX  0xEE
#define X86_OUT_AX_DX  0xEF

#define X86_OUTSB  0x6E
#define X86_OUTSW  0x6F

v86_mcontext_t v86_call_bios(unsigned char irq, v86_mcontext_t regs) {
    unsigned char * instructions = (void *)X86_CONVENTIONAL_MEMORY_START;
    instructions[0] = X86_INT;
    instructions[1] = irq;
    instructions[2] = X86_INT;
    instructions[3] = V86_TERMINATING_INTERRUPT;
    return run_v86_task(NULL, 0, 0, regs);
}

// note: we don't have to check for segment boundaries when setting SP and IP
// because upper 16 bits are discarded anyway when calling iret

static unsigned short v86_pop_word_from_stack(v86_mcontext_t * ctx) {
    if (X86_SEGMENT_SIZE - (unsigned long)ctx->iret_frame.sp < sizeof(unsigned short))
        panic("Stack segment overrun on a Virtual-8086 kernel task, cannot recover!");

    void * esp = V86_FAR2LIN(ctx->iret_frame.ss, ctx->iret_frame.sp);
    unsigned short value = *(unsigned short *)esp;
    ctx->iret_frame.sp += sizeof(unsigned short);
    return value;
}

static void v86_push_word_to_stack(unsigned short value, v86_mcontext_t * ctx) {
    if ((unsigned long)ctx->iret_frame.sp < sizeof(unsigned short))
        panic("Stack segment underrun on a Virtual-8086 kernel task, cannot recover!");

    ctx->iret_frame.sp -= sizeof(unsigned short);
    void * esp = V86_FAR2LIN(ctx->iret_frame.ss, ctx->iret_frame.sp);
    *(unsigned short *)esp = value;
}

static unsigned short v86_transform_flags_pushf(unsigned long eflags) {
    unsigned short flags = eflags & 0xFFFF;
    if (eflags & IA_32_EFL_VIRT_INTER)
        return flags | IA_32_EFL_SYSTEM_INTER_EN;
    return flags & ~IA_32_EFL_SYSTEM_INTER_EN;
}
static unsigned long v86_transform_flags_popf(unsigned short flags, unsigned long proper_eflags) {
    unsigned long eflags = flags & 0x0000FFFF;
    eflags |= proper_eflags      & 0xFFFF0000;

    // otherwise GP would triple fault
    eflags |= IA_32_EFL_SYSTEM_INTER_EN;
    // cuz we are running in 8086 mode
    eflags |= IA_32_EFL_SYSTEM_VM8086;

    if (flags & IA_32_EFL_SYSTEM_INTER_EN)
        return eflags | IA_32_EFL_VIRT_INTER;
    return eflags & ~IA_32_EFL_VIRT_INTER;
}

void v86_monitor(unsigned long error, v86_mcontext_t * ctx) {
    char is_o32 = 0;

    void * eip = V86_FAR2LIN(ctx->iret_frame.cs, ctx->iret_frame.ip);
    unsigned char faulting_instruction = *(unsigned char *)eip; // technically not safe, but this address will always be mapped
    unsigned char op1 = *(unsigned char *)(eip + 1);
    void * string_ptr = V86_FAR2LIN(ctx->iret_frame.es, ctx->edi);

    //kprintf("handling instruction %hhx @ %p\n", faulting_instruction, eip);
    rerun:
    switch (faulting_instruction) {
        case X86_O32:
            is_o32 = 1;
            ctx->iret_frame.ip++;
            eip++;
            faulting_instruction = *(unsigned char *)eip;
            op1 = *(unsigned char *)(eip + 1);
            goto rerun;
        case X86_CLI:
            ctx->iret_frame.ip++;
            ctx->iret_frame.flags &= ~IA_32_EFL_VIRT_INTER;
            return;
        case X86_STI:
            ctx->iret_frame.ip++;
            ctx->iret_frame.flags |= IA_32_EFL_VIRT_INTER;
            return;
        case X86_PUSHF:
            ctx->iret_frame.ip++;
            v86_push_word_to_stack(v86_transform_flags_pushf(ctx->iret_frame.flags), ctx);
            if (is_o32) v86_push_word_to_stack(0, ctx); // no additional flags relevant anyway
            return;
        case X86_POPF:
            ctx->iret_frame.ip++;
            if (is_o32) v86_pop_word_from_stack(ctx);
            ctx->iret_frame.flags = v86_transform_flags_popf(v86_pop_word_from_stack(ctx), ctx->iret_frame.flags);
            return;

        case X86_INTO:
            op1 = 4;
            ctx->iret_frame.ip -= 1; // counteract the +1 from X86_INT, since INT3 and INT0 are 1 byte instructions
            goto interrupt;
        case X86_INT3:
            op1 = 3;
            ctx->iret_frame.ip -= 1;
        case X86_INT:
            interrupt:
            if (op1 == V86_TERMINATING_INTERRUPT) {
                kassert(waiting_thread);
                //kprintf("Virtual-8086: Task exited using interrupt %hhx\n", op1);
                pending_context = *ctx;
                spinlock_acquire(&scheduler_lock); // to not preempt when only one is set
                waiting_thread->status = SCHED_RUNNABLE;
                current_thread->status = SCHED_V86_THREAD_CLEANUP;
                spinlock_release(&scheduler_lock);
                reschedule();
            }
            //kprintf("Virtual-8086: interrupt %hhx\n", op1);
            v86_push_word_to_stack(v86_transform_flags_pushf(ctx->iret_frame.flags), ctx);
            v86_push_word_to_stack((unsigned short)ctx->iret_frame.cs, ctx);
            v86_push_word_to_stack((unsigned short)(unsigned long)ctx->iret_frame.ip + 2, ctx);
            // any decent static analyzer will tell you this is a possible null pointer dereference
            // this is by design, the IVT is really located at 0x0000 - 0x03FF
            ctx->iret_frame.flags &= ~IA_32_EFL_VIRT_INTER;
            ctx->iret_frame.cs = real_mode_ivt[op1 * 2 + 1];
            ctx->iret_frame.ip = (void *)V86_FAR_MAKE(ctx->iret_frame.cs, real_mode_ivt[op1 * 2 + 0]);
            //kprintf("returning to %hx:%hx\n", ctx->iret_frame.cs, ctx->iret_frame.ip);
            return;
        case X86_IRET:
            //kprintf("Virtual-8086: returning from interrupt\n");
            ctx->iret_frame.ip    = (void *)(unsigned long)v86_pop_word_from_stack(ctx);
            ctx->iret_frame.cs    =                        v86_pop_word_from_stack(ctx);
            ctx->iret_frame.flags = v86_transform_flags_popf(v86_pop_word_from_stack(ctx), ctx->iret_frame.flags);
            return;
        case X86_IN_AL_IMM:
            ctx->iret_frame.ip += 2;
            ctx->eax = inb(op1);
            return;
        case X86_IN_AX_IMM:
            ctx->iret_frame.ip += 2;
            ctx->eax = inw(op1);
            return;
        case X86_IN_AL_DX:
            ctx->iret_frame.ip++;
            ctx->eax = inb(ctx->edx);
            return;
        case X86_IN_AX_DX:
            ctx->iret_frame.ip++;
            ctx->eax = inw(ctx->edx);
            return;
        case X86_INSB:
            ctx->iret_frame.ip++;
            *(unsigned char *)string_ptr = inb(ctx->edx);
            ctx->edi -= ctx->iret_frame.flags & IA_32_EFL_DIRECTION ? 1 : -1;
            return;
        case X86_INSW:
            ctx->iret_frame.ip++;
            *(unsigned short *)string_ptr  = inw(ctx->edx);
            ctx->edi -= ctx->iret_frame.flags & IA_32_EFL_DIRECTION ? 2 : -2;
            return;

        case X86_OUT_AL_IMM:
            ctx->iret_frame.ip += 2;
            outb(op1, ctx->eax);
            return;
        case X86_OUT_AX_IMM:
            ctx->iret_frame.ip += 2;
            outw(op1, ctx->eax);
            return;
        case X86_OUT_AL_DX:
            ctx->iret_frame.ip++;
            outb(ctx->edx, ctx->eax);
            return;
        case X86_OUT_AX_DX:
            ctx->iret_frame.ip++;
            outw(ctx->edx, ctx->eax);
            return;
        case X86_OUTSB:
            ctx->iret_frame.ip++;
            outb(ctx->edx, *(unsigned char *)string_ptr);
            ctx->edi -= ctx->iret_frame.flags & IA_32_EFL_DIRECTION ? 1 : -1;
            return;
        case X86_OUTSW:
            ctx->iret_frame.ip++;
            outb(ctx->edx, *(unsigned short *)string_ptr);
            ctx->edi -= ctx->iret_frame.flags & IA_32_EFL_DIRECTION ? 2 : -2;
            return;
        default:
            kprintf("error %lx, efl %lx, ip %p, cs %lx, ins: %hhx\n", error, ctx->iret_frame.flags, ctx->iret_frame.ip, ctx->iret_frame.cs, faulting_instruction);
            panic("#GP on unknown instruction in a Virtual-8086 kernel task, cannot recover!");
    }
}