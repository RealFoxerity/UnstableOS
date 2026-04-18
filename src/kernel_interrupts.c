#include "include/kernel_interrupts.h"
#include "include/debug/backtrace.h"
#include "include/kernel_gdt_idt.h"
#include "include/kernel_sched.h"
#include "include/ps2_controller.h"
#include "include/lowlevel.h"
#include "include/kernel.h"
#include "include/timer.h"
#include "include/vga.h"
#include "../libc/src/include/errno.h"
#include "../libc/src/include/string.h"
#include "kernel_console.h" // for the console cursor blinking
#include <stdint.h>

// TODO: rewrite everything to a single dispatcher that correctly fixes all segments

#pragma clang diagnostic ignored "-Wexcessive-regsave" // compiling with -mregular-regs-only anyway
#pragma clang diagnostic ignored "-Wc99-designator"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"
const char reserved_idt_interr_has_error[RES_INTERR_EXCEPTION_COUNT] = {
    [INT_ABORT_DOUBLE_FAULT] = 1,
    [INT_FAULT_INVALID_TSS] = 1,
    [INT_FAULT_SEGMENT_NOT_PRESENT] = 1,
    [INT_FAULT_STACK_SEGMENT_FAULT] = 1,
    [INT_FAULT_GENERAL_PROTECTION] = 1,
    [INT_FAULT_PAGE_FAULT] = 1,
    [INT_FAULT_ALIGNMENT_CHECK] = 1,
    [INT_FAULT_CONTROL_PROT_EXCEPTION] = 1,
};

extern uint8_t console_x, console_y;

extern spinlock_t gfx_spinlock;
void clear_screen_fatal() {
    gfx_spinlock.state = SPINLOCK_UNLOCKED;
    vga_clear_screen();
    for (int i = 0; i < display_width; i += console_font_width*4) {
        for (int j = 0; j < display_height; j += console_font_height*4) {
            // 19 = double exclamation
            gfx_blit_char_buffered(19,
                i, j,
                CONSOLE_COLOR_RED,
                CONSOLE_COLOR_BRIGHT_RED,
                1,
                4);
        }
    }
    gfx_swap_buffers();
    kprintf("\e[H");
}

void print_segment_selector_error(unsigned long error) {
    if (error & 1) kprintf("EXTERNAL: "); // event external to the application caused this (e.g. hardware interrupt handler)
    switch ((error&0b110)>>1) {
        case 0:
            kprintf("GDT ");
            break;
        case 1:
            kprintf("IDT ");
            break;
        case 2:
            kprintf("LDT ");
            break;
        case 3:
            kprintf("IDT ");
            break;
    }
    kprintf("index %lu\n", (error >> 3)&((1<<13)-1));
}

static void print_segment(unsigned long segment) {
    kprintf("segment %s index %lu priv level %lu\n", segment&4?"LDT":"GDT", (segment&0xFFFF)>>3, segment&3);
}

static inline void print_eflags(uint32_t eflags) {
    kprintf("(%lx) ", (unsigned long)eflags);
    if (eflags & IA_32_EFL_STATUS_CARRY) kprintf("CF ");
    if (eflags & IA_32_EFL_STATUS_PARITY) kprintf("PF ");
    if (eflags & IA_32_EFL_STATUS_ADJUST) kprintf("AF ");
    if (eflags & IA_32_EFL_STATUS_ZERO) kprintf("ZF ");
    if (eflags & IA_32_EFL_STATUS_SIGN) kprintf("SF ");
    if (eflags & IA_32_EFL_SYSTEM_TRAP) kprintf("TF ");
    if (eflags & IA_32_EFL_SYSTEM_INTER_EN) kprintf("IF ");
    if (eflags & IA_32_EFL_DIRECTION) kprintf("DF ");
    if (eflags & IA_32_EFL_STATUS_OVERFLOW) kprintf("OF ");
    if (eflags & IA_32_EFL_SYSTEM_IO_PRIV) kprintf("IOPF ");
    if (eflags & IA_32_EFL_SYSTEM_NESTED_TASK) kprintf("NT ");
    if (eflags & IA_32_EFL_SYSTEM_RESUME) kprintf("RF ");
    if (eflags & IA_32_EFL_SYSTEM_VM8086) kprintf("VM ");
    if (eflags & IA_32_EFL_SYSTEM_ALIGN_CHECK) kprintf("ACF ");
    if (eflags & IA_32_EFL_VIRT_INTER) kprintf("VIF ");
    if (eflags & IA_32_EFL_VIRT_INTER_PEND) kprintf("VIP ");
    if (eflags & IA_32_EFL_CPUID) kprintf("ID ");
}

void print_interr_frame(struct interr_frame * interr_frame) {
    struct symbol_lookup instruction = resolve_symbol(interr_frame->ip);
    kprintf("EIP:\t%s+%p [%p]\n", instruction.symbol, instruction.addr_offset, interr_frame->ip);
    kprintf("CS:\t");
    print_segment(interr_frame->cs);

    if ((interr_frame->cs & 3) != 0) { // interrupts don't push SS and SP when not changing ring level (0[kernel] -> 0[interrupt])
        kprintf("ESP:\t%p\nSS:\t", interr_frame->sp);
        print_segment(interr_frame->ss);
    }
    kprintf("EFLAGS:\t");
    print_eflags(interr_frame->flags);
    kprintf("\n");
}

void divide_error_handler(mcontext_t * ctx) {
    kprintf("\n\n\e[0m\e[41m#### ISR: FPE caught! ####\e[0m\n\n");
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));

    memcpy(&current_thread->context, ctx, sizeof(mcontext_t) - (ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *));
    signal_thread(current_process, current_thread, &(siginfo_t){
        .si_signo = SIGFPE,
        // TODO: add si_code
    });
    signal_dispatch_sa(current_process, current_thread);
    memcpy(ctx, &current_thread->context, sizeof(mcontext_t) - (ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *));
}

// when doing v86, the segments are wrong for normal protected (32bit) mode
// so they get zeroed out (a bad thing) which inevitably causes GPF
// 0x23 here being the number for the user's data segment
// why the user segment? the kernel can still use it
// and it makes it easier to return from interrupts
// we use a flat model, so it doesn't matter anyway
__attribute__((naked)) void fix_segments() {
    asm volatile (
        "pushl %eax;"
        "mov $0x23, %ax;"
        "mov %ax, %ds;"
        "mov %ax, %es;"
        "mov %ax, %fs;"
        "mov %ax, %gs;"
        "popl %eax;"
        "ret;"
    );
}

// we need to change the ctx for signal dispatching to work
__attribute__((naked, no_caller_saved_registers)) static void interr_divide_error(struct interr_frame * interrupt_frame) {
    asm volatile (
        "cld;"
        "call fix_segments;"
        "pusha;"
        "pushl %esp;"
        "call divide_error_handler;"
        "popl %esp;"
        "popa;"
        "call reschedule;"
        "iret;"
    );
}

__attribute__((interrupt, no_caller_saved_registers)) static void interr_debug_trap(struct interr_frame * interrupt_frame) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: DEBUG caught! ####\e[0m\n\n");
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));
    //panic("Debug");
}

#define NMI_RAM_ABORT " #### ISR: PANIC - RAM ERROR - NMI PARITY; HALTING #### "
#define NMI_CHANNEL_ABORT " #### ISR: PANIC - BUS ERROR - NMI CHANNEL; HALTING #### "
#define NMI_WATCHDOG_ABORT " #### ISR: PANIC - WATCHDOG TIMER RAN OUT; HALTING #### "
__attribute__((interrupt, no_caller_saved_registers)) static void interr_nmi(struct interr_frame * interrupt_frame) {
    fix_segments();
    uint8_t portA = inb(NMI_INTERR_CONTROL_PORT_A);
    uint8_t portB = inb(NMI_INTERR_CONTROL_PORT_B);
    if (portB & NMI_CONTROL_B_PARITY_CHECK || portB & NMI_CONTROL_B_CHANNEL_CHECK) {
        clear_screen_fatal();
        if (portB & NMI_CONTROL_B_PARITY_CHECK) {
            panic(NMI_RAM_ABORT);
        } else {
            panic(NMI_CHANNEL_ABORT);
        }
        __builtin_unreachable();
    }
    if (portA & NMI_CONTROL_A_WATCHDOG_TIMER) {
        panic(NMI_WATCHDOG_ABORT);
        __builtin_unreachable();
    }

    kprintf("\n\n\e[0m\e[41m#### ISR: NMI caught! ####\e[0m\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_breakpoint(struct interr_frame * interrupt_frame) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: Breakpoint caught! ####\e[0m\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_overflow(struct interr_frame * interrupt_frame) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: Integer overflow (INTO) caught! ####\e[0m\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_bound_range_ex(struct interr_frame * interrupt_frame) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: Bound index outside of range! ####\e[0m\n\n");
}

void invalid_opcode_handler(mcontext_t * ctx) {
    kprintf("\n\e[0m\e[41m\n#### ISR: Tried to execute invalid opcode at %p! ####", ctx->iret_frame.ip);
    print_interr_frame(&ctx->iret_frame);

    scheduler_print_process(current_process);
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));
    if (current_process->pid == 0) {
        panic("Kernel task cannot be recovered from invalid instruction exception");
        __builtin_unreachable();
    }

    memcpy(&current_thread->context, ctx, sizeof(mcontext_t) - (ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *));
    signal_thread(current_process, current_thread, &(siginfo_t){
        .si_signo = SIGILL,
        // TODO: add si_code
    });
    signal_dispatch_sa(current_process, current_thread);
    memcpy(ctx, &current_thread->context, sizeof(mcontext_t) - (ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *));
    kprintf("\e[0m\n\n");
}
__attribute__((naked, no_caller_saved_registers)) static void interr_invalid_opcode(struct interr_frame * interrupt_frame) {
    asm volatile (
        "cld;"
        "call fix_segments;"
        "pusha;"
        "pushl %esp;"
        "call invalid_opcode_handler;"
        "popl %esp;"
        "popa;"
        "call reschedule;"
        "iret;"
    );
}

__attribute__((interrupt, no_caller_saved_registers)) static void interr_dev_not_avail(struct interr_frame * interrupt_frame) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: FPU Coprocessor not present or not ready! ####\e[0m\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_double_fault_abort(struct interr_frame * interrupt_frame, unsigned long error) {
    fix_segments();
    clear_screen_fatal();

    kprintf("\e[0m\e[41m#### ISR: CRITICAL - CAUGHT A DOUBLE FAULT! ####\e[0m\n");
    print_interr_frame(interrupt_frame);
    scheduler_print_process(current_process);
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));

    panic("CANNOT RECOVER FROM A DOUBLE FAULT");
    /* // being here means we ran into an exception during an interrupt so most likely trying to switch to a process has/will fail/ed
    kprintf("\n\n");
    if (current_process->pid == 0) {
        panic("Kernel task cannot be recovered from a double fault");
        __builtin_unreachable();
    } else {
        kprintf("Terminating process\n", current_process->pid);
        scheduler_print_process(current_process);
        current_process->status = SCHED_CLEANUP;
    }
    vga_set_color(old_tty_color&0x0F, (old_tty_color&0xF0) >> 4);

    interrupt_frame->ip = kernel_idle;
    interrupt_frame->cs = GDT_KERNEL_CODE << 3;
    interrupt_frame->ss = GDT_KERNEL_DATA << 3;
    */
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_coprocessor_segment_overrun(struct interr_frame * interrupt_frame) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: FPU coprocessor memory segment overran! ####\e[0m\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_invalid_tss(struct interr_frame * interrupt_frame, unsigned long error) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: TASK SWITCH ENCOUTERED INVALID TSS ENTRY! ####\n");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));
    kprintf("\e[0m");
    panic("System fault");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_segment_not_present(struct interr_frame * interrupt_frame, unsigned long error) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: REFERENCED MEMORY SEGMENT IS NOT PRESENT! ####\n");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));
    kprintf("\e[0m");
    panic("System fault");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_stack_segment_fault(struct interr_frame * interrupt_frame, unsigned long error) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: REFERENCE STACK ADDRESS OUTSIDE STACK SEGMENT! ####\n");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));
    kprintf("\e[0m");
    panic("We don't do segmented model and yet the stack is outside SS? System fault");
}
extern struct idt_gate * idt_descriptor_entries;

__attribute__((no_caller_saved_registers)) void gp_print_info(struct interr_frame * interrupt_frame, unsigned long error) {
    if (!(interrupt_frame->cs & 3))
        clear_screen_fatal();
    kprintf("\n\e[0m\e[41m\n#### ISR: Segmentation fault - Protection violation! ####\n");
    kprintf("SEL ERR:\t");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);
    scheduler_print_process(current_process);
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));
}

void general_protection_handler(mcontext_t * ctx) {
    if (!(ctx->iret_frame.cs & 3)) {
        panic("Kernel task cannot be recovered from a segmentation fault");
        __builtin_unreachable();
    }

    memcpy(&current_thread->context, ctx, sizeof(mcontext_t) - (ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *));
    signal_thread(current_process, current_thread, &(siginfo_t){
        .si_signo = SIGSEGV,
        .si_code  = SEGV_ACCERR
    });
    signal_dispatch_sa(current_process, current_thread);
    memcpy(ctx, &current_thread->context, sizeof(mcontext_t) - (ctx->iret_frame.cs & 3) ? 0 : 2 * sizeof(void *));
}

#include "v8086.h"
__attribute__((naked, no_caller_saved_registers)) static void interr_general_protection(struct interr_frame * interrupt_frame, unsigned long error) {
    asm volatile (
        "cld;"
        "call fix_segments;"
        "pushl 0xC(%esp);" // eflags from the interrupt frame
        "andl $0x20000, (%esp);" // vm8086 flag
        "jnz v86;"

        "addl $0x4, %esp;"
        "pushl %esp;"
        "addl $0x4, (%esp);"
        "call gp_print_info;"
        "addl $0x8, %esp;" // remove the arguments
        "pusha;"
        "pushl %esp;"
        "call general_protection_handler;"
        "addl $0x4, %esp;"
        "popa;"
        "call reschedule;" // in case this would be a termination
        "iret;"

        "v86:"
        "addl $0x4, %esp;"
        "pushl %eax;"
        "movl 0x4(%esp), %eax;" // the error value
        "movl %eax, -0x20(%esp);" // after where the pusha and push %esp would end
        "popl %eax;"
        "addl $0x4, %esp;"
        "pusha;"
        "pushl %esp;" // the ctx argument
        "sub $0x4, %esp;" // readjust so that error is "inside the stack"
        "call v86_monitor;"
        "addl $0x8, %esp;" // get rid of arguments
        "popa;"
        "iret;"
    );
}
/*
__attribute__((interrupt, no_caller_saved_registers)) static void interr_x87_float_error(struct interr_frame * interrupt_frame, unsigned long error) {
    kprintf("\n\n\e[0m\e[41m#### ISR: x87 FPE! ####\e[0m\n\n");
}
*/
__attribute__((interrupt, no_caller_saved_registers)) static void interr_alignment_check(struct interr_frame * interrupt_frame, unsigned long error) {
    fix_segments();
    kprintf("\n\n\e[0m\e[41m#### ISR: Caught unaligned memory access! ####\n");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);
    unwind_stack_vaddr(*(void**)__builtin_frame_address(0));
    kprintf("\e[0m");
}

#define CHECK_MACHINE_ERROR " #### ISR: PANIC - INTERNAL HW ERROR - CHECK MACHINE; HALTING #### "
__attribute__((interrupt, no_caller_saved_registers)) static void interr_machine_check_abort(struct interr_frame * interrupt_frame, unsigned long error) {
    fix_segments();
    clear_screen_fatal();
    kprintf("\e[%d;0f", display_height_chars/2);
    panic(CHECK_MACHINE_ERROR);
    __builtin_unreachable();
}
//__attribute__((interrupt, no_caller_saved_registers)) void interr_simd_fpe(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_virtualization_exception(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_control_prot_exception(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_hypervisor_injection_exception(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_vmm_communication_exception(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_security_exception(struct interr_frame * interrupt_frame);


__attribute__((interrupt, no_caller_saved_registers)) void general_fault_handler_error(struct interr_frame * interrupt_frame, unsigned long error) { // ul to shut clang
    fix_segments();
    kprintf("\e[0m\e[41mPANIC: UNREGISTERED CPU FAULT, ERR CODE %lx\e[0m\n", error);
}

__attribute__((interrupt, no_caller_saved_registers)) void general_fault_handler_no_error(struct interr_frame * interrupt_frame) { // ul to shut clang
    fix_segments();
    kprintf("\e[0m\e[41mPANIC: UNREGISTERED CPU FAULT, ERR CODE\e[0m\n");
}


const void* cpu_interr_handlers[RES_INTERR_EXCEPTION_COUNT] = {
    [INT_FAULT_DIVIDE_ERROR] = interr_divide_error,
    [INT_DEBUG_EXCEPTION] = interr_debug_trap,
    [INT_INT_NMI_INTERRUPT] = interr_nmi,
    [INT_TRAP_BREAKPOINT] = interr_breakpoint,
    [INT_TRAP_OVERFLOW] = interr_overflow,
    [INT_FAULT_BOUND_RANGE_EXCEEDED] = interr_bound_range_ex,
    [INT_FAULT_INVALID_OP] = interr_invalid_opcode,
    [INT_FAULT_DEV_NOT_READY] = interr_dev_not_avail,
    [INT_ABORT_DOUBLE_FAULT] = interr_double_fault_abort,
    [INT_FAULT_COPROCESSOR_SEG_OVERRUN] = interr_coprocessor_segment_overrun,
    [INT_FAULT_INVALID_TSS] = interr_invalid_tss,
    [INT_FAULT_SEGMENT_NOT_PRESENT] = interr_segment_not_present,
    [INT_FAULT_STACK_SEGMENT_FAULT] = interr_stack_segment_fault,
    [INT_FAULT_GENERAL_PROTECTION] = interr_general_protection,
    [INT_FAULT_PAGE_FAULT] = interr_page_fault,
    //[INT_FAULT_X87_FLOAT_ERROR] = interr_x87_float_error,
    [INT_FAULT_X87_FLOAT_ERROR] = interr_divide_error,
    [INT_FAULT_ALIGNMENT_CHECK] = interr_alignment_check,
    [INT_ABORT_MACHINE_CHECK] = interr_machine_check_abort,
    //[INT_FAULT_SIMD_FPE] = (uint32_t) interr_simd_fpe,
    //[INT_FAULT_VIRTUALIZATION_EXCEPTION] = (uint32_t) interr_virtualization_exception,
    //[INT_FAULT_CONTROL_PROT_EXCEPTION] = (uint32_t) interr_control_prot_exception,
    //[INT_FAULT_HYPERVISOR_INJECTION_EX] = (uint32_t) interr_hypervisor_injection_exception,
    //[INT_FAULT_VMM_COMMUNICATION_EX] = (uint32_t) interr_vmm_communication_exception,
    //[INT_FAULT_SECURITY_EXCEPTION] = (uint32_t) interr_security_exception,
};

void pic_mask_irq(uint8_t irq_num) {
    uint16_t port;
    if (irq_num < 8) {
        port = PIC_M_DATA_ADDR;
    } else {
        port = PIC_S_DATA_ADDR;
        irq_num -= 8;
    }

    uint8_t mask = inb(port);
    mask |= 1<<irq_num;
    outb(port, mask);
}

void pic_unmask_irq(uint8_t irq_num) {
    uint16_t port;
    if (irq_num < 8) {
        port = PIC_M_DATA_ADDR;
    } else {
        port = PIC_S_DATA_ADDR;
        irq_num -= 8;
    }

    uint8_t mask = inb(port);
    mask &= ~(1<<irq_num);
    outb(port, mask);
}


// ISR = in-service register - what has been sent to the cpu to handle
// IRR = interrupt request register - what has been raised (basically a queue)
// Note: we are using the 8086 mode instead of the 8080 of the pic
// ocw1 = disabled interrupts mask, is sent to DATA, what we do in pic_*mask_irq
// ocw2 = general runtime commands
// ocw3 = runtime query
// icw1 = what they do
// icw2 = where they do it
// icw3 = how do they communicate
// icw4 = special flags
enum pic_ocw2 {
    // 3 bit value depicting IRQ number in specific mode
    // 2 zeroes
    PIC_OCW2_EOI = 0x20,
    PIC_OCW2_SPECIFIC = 0x40, // operation is specific to irq number from the first 3 bits
    PIC_OCW2_ROTATE = 0x80, // rotate, as in shift register, the irq numbers; i think
};
enum pic_ocw3 {
    PIC_OCW3_READ_ISR = 1, // otherwise return IRR; READ_REG has to be set
    PIC_OCW3_READ_REG = 2,
    PIC_OCW3_POLL = 4, // basically, you do this and then use the following byte as a custom irq number from 0 to 64
    PIC_OCW3_ALWAYS_1 = 0x8,
    PIC_OCW3_SET_SPECIAL_MASK = 0x20, // otherwise reset
    PIC_OCW3_ALTER_SPECIAL_MASK = 0x40,
};
enum pic_icw1 {
    PIC_ICW1_ICW4_NEEDED = 1,
    PIC_ICW1_SINGLE = 2, // or cascade
    PIC_ICW1_INT_VEC_DIST_4_BYTES = 4, // interrupt interval, otherwise 8 bytes between interrupt handlers
    PIC_ICW1_LEVEL_TRIGGERED = 8, // if set, interrupts are considered active if they are HELD high, otherwise triggered/toggled by a high pulse
    PIC_ICW1_ALWAYS_1 = 0x10
    //bits 5, 6, 7 of page address of interrupt table start, in 8086 all zeroes
    //always 0
};
// icw2 = T7, T6, T5, T4, T3, 0 0 0, where Ts are offsets into IDT
// icw3_m = irq number for slave pic (mask)
// icw3_s = 0, 0, 0, 0, 0, X, X, X the same but as a bit number (as in (irq) 3 instead of the irq mask)
enum pic_icw4 {
    PIC_ICW4_8086_MODE = 1,
    PIC_ICW4_AUTO_EOI = 2,
    PIC_ICW4_BUFFERING_MASTER = 4,
    PIC_ICW4_BUFFERING = 8,
    PIC_ICW4_SPECIAL_FNESTED = 0x10
};

static inline char pic_is_spurious(uint8_t irq) {
    uint16_t port;
    if (irq < 8) port = PIC_M_COMM_ADDR;
    else {
        irq -= 8;
        port = PIC_S_COMM_ADDR;
    }

    outb(port, PIC_OCW3_READ_REG | PIC_OCW3_READ_ISR | PIC_OCW3_ALWAYS_1);
    uint8_t isr = inb(port);

    return (isr & (1 << irq)) != 0;
}

__attribute__((no_caller_saved_registers)) void pic_send_eoi(uint8_t irq) {
    uint16_t port;
    if (irq < 8) port = PIC_M_COMM_ADDR;
    else {
        outb(PIC_M_COMM_ADDR, PIC_OCW2_EOI | PIC_OCW2_SPECIFIC | PIC_INTERR_CASCADE);
        irq -= 8;
        port = PIC_S_COMM_ADDR;
    }

    outb(port, PIC_OCW2_EOI | PIC_OCW2_SPECIFIC | (irq & 7));
}

__attribute__((no_caller_saved_registers)) void pic_send_eoi_all() {
    fix_segments();
    outb(PIC_M_COMM_ADDR, PIC_OCW2_EOI);
    outb(PIC_S_COMM_ADDR, PIC_OCW2_EOI);
}


__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_default(struct interr_frame * interrupt_frame) {
    fix_segments();
    outb(PIC_M_COMM_ADDR, PIC_OCW2_EOI);
    outb(PIC_S_COMM_ADDR, PIC_OCW2_EOI);
}

__attribute__((naked, no_caller_saved_registers)) void interr_pic_pit(struct interr_frame * interrupt_frame) {
    asm volatile(
        "cld\n\t"
        "call fix_segments\n\t"
        "pusha\n\t"
        //"pushl %ebp\n\t" // handled by pusha
        "movl %esp, %ebp\n\t"

        //"addl $"STR(KERNEL_TIMER_RESOLUTION_MSEC)", uptime_msec\n\t" // not that accurate, using rtc instead

        "pushl %esp\n\t"

        "call schedule\n\t"

        "popl %eax\n\t" // get rid of argument

        "pushl $0x0\n\t" //PIC_INTERR_PIT, can't stringify enum :(
        "call pic_send_eoi\n\t"
        "popl %eax\n\t"

        "popa\n\t" // popa DISCARDS ESP so we need to manually load it

        "movl -0x14(%esp), %esp\n\t"
        // see pusha register order, we have to set the ESP from the return value when returning to a ring 0 process
        // also return to outer CPL requires pushing ESP and SS onto the stack, which would overwrite data if a
        // ring 0 process was running, so we load the iret frame into the ring 3's stack

        "iret\n\t"
    );
}


__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_keyboard(struct interr_frame * interrupt_frame) {
    fix_segments();
    ps2_driver(1);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_mouse(struct interr_frame * interrupt_frame) {
    fix_segments();
    ps2_driver(2);
}

extern void com_recv_byte(char com);
__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_com2(struct interr_frame * interrupt_frame) {
    fix_segments();
    com_recv_byte(1);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_com1(struct interr_frame * interrupt_frame) {
    fix_segments();
    com_recv_byte(0);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_lpt2(struct interr_frame * interrupt_frame) {
    fix_segments();
    pic_send_eoi(PIC_INTERR_LPT2);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_floppy(struct interr_frame * interrupt_frame) {
    fix_segments();
    pic_send_eoi(PIC_INTERR_FLOPPY);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_lpt1(struct interr_frame * interrupt_frame) {
    fix_segments();
    if (pic_is_spurious(PIC_INTERR_LPT1)) return;

    pic_send_eoi(PIC_INTERR_LPT1);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_cmos_rtc(struct interr_frame * interrupt_frame) {
    fix_segments();
    enum rtc_interrupt_bitmasks called_ints = rtc_get_last_interrupt_type();

    if (called_ints & RTC_INT_PERIODIC) {
        // this is bad, not atomic; i386 unfortunately doesn't really have atomic 64 bit increments
        // should be fine considering we're only doing this in this interrupt
        if (interrupt_frame->cs & 3) {
            current_process->user_clicks ++;
        } else {
            current_process->system_clicks ++;
        }
        uptime_clicks ++;
        sleep_sched_tick();

        console_blink_cursor();
    }
    if (called_ints & RTC_INT_ALARM) {
        kprintf("Recieved RTC alarm interrupt\n");
    }
    if (called_ints & RTC_INT_UPDATE_ENDED) {
        system_time_sec ++;
        if (system_time_sec % 120 == 0) { // to account for potencial interrupt/syscall/whatever drift manually sync time every 2 minutes
            extern time_t rtc_get_time();
            system_time_sec = rtc_get_time();
        }
    }
    pic_send_eoi(PIC_INTERR_CMOS_RTC);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_sec_ata(struct interr_frame * interrupt_frame) {
    if (pic_is_spurious(PIC_INTERR_SECONDARY_ATA)) return;

    pic_send_eoi(PIC_INTERR_SECONDARY_ATA);
}

const void * pic_interr_handlers[PIC_INTERR_COUNT] = {
    [PIC_INTERR_PIT] = interr_pic_pit,
    [PIC_INTERR_KEYBOARD] = interr_pic_keyboard,
    //[PIC_INTERR_CASCADE] =
    [PIC_INTERR_COM2] = interr_pic_com2,
    [PIC_INTERR_COM1] = interr_pic_com1,
    //[PIC_INTERR_LPT2] = interr_pic_lpt2,
    //[PIC_INTERR_FLOPPY] = interr_pic_floppy,
    [PIC_INTERR_LPT1] = interr_pic_lpt1, // spurious, dont know if I can mask spurious interrupts...
    [PIC_INTERR_CMOS_RTC] = interr_cmos_rtc,
    //[PIC_INTERR_USER_1] =
    //[PIC_INTERR_USER_2] =
    //[PIC_INTERR_USER_3] =
    [PIC_INTERR_PS2_MOUSE] = interr_pic_mouse,
    //[PIC_INTERR_COPROCESSOR] =
    //[PIC_INTERR_PRIMARY_ATA] =
    [PIC_INTERR_SECONDARY_ATA] = interr_pic_sec_ata, // spurious
};

void pic_setup(uint8_t lower_idt_off, uint8_t higher_idt_off) {

    outb(PIC_M_DATA_ADDR, 0xFF); // masks all interrupts
    outb(PIC_S_DATA_ADDR, 0xFF);

    outb(PIC_M_COMM_ADDR, PIC_ICW1_ICW4_NEEDED | PIC_ICW1_ALWAYS_1);
    io_wait(); // pic can take some time to take in the data
    outb(PIC_S_COMM_ADDR, PIC_ICW1_ICW4_NEEDED | PIC_ICW1_ALWAYS_1);
    io_wait();

    outb(PIC_M_DATA_ADDR, lower_idt_off  & 0xF8); // will never realistically be unaligned to this, but to be safe
    io_wait();
    outb(PIC_S_DATA_ADDR, higher_idt_off & 0xF8);
    io_wait();

    outb(PIC_M_DATA_ADDR, 1 << PIC_INTERR_CASCADE); // Master needs a mask
    outb(PIC_S_DATA_ADDR, PIC_INTERR_CASCADE);

    outb(PIC_M_DATA_ADDR, PIC_ICW4_8086_MODE);
    io_wait();
    outb(PIC_S_DATA_ADDR, PIC_ICW4_8086_MODE);
    io_wait();
}


void disable_interrupts() {
    outb(PIC_M_DATA_ADDR, 0xFF); // masks all interrupts
    outb(PIC_S_DATA_ADDR, 0xFF);
    disable_nmi();
    asm volatile ("cli");
}
void enable_interrupts() {
    enable_nmi();
    asm volatile ("sti");
    for (int i = 0; i < PIC_INTERR_COUNT; i++) {
        if (pic_interr_handlers[i] == 0 && i != PIC_INTERR_CASCADE) pic_mask_irq(i);
        else pic_unmask_irq(i);
    }
}
