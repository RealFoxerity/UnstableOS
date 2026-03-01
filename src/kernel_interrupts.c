#include "include/kernel_interrupts.h"
#include "include/devs.h"
#include "include/kernel_gdt_idt.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "include/mm/kernel_memory.h"
#include "include/ps2_keyboard.h"
#include "include/lowlevel.h"
#include "include/kernel.h"
#include "include/timer.h"
#include "include/vga.h"
#include "../libc/src/include/string.h"
#include "include/kernel_tty.h"
#include "include/errno.h"
#include "include/block/memdisk.h"
#include <stdint.h>

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

extern uint8_t vga_x, vga_y;

void clear_screen_fatal() {
    vga_enable_blink();
    vga_disable_cursor();
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_RED | VGA_COLOR_BLINK);
    uint8_t color = vga_get_color();
    //memset((uint16_t*)VGA_TEXT_MODE_ADDR, vga_get_color(), VGA_WIDTH*VGA_HEIGHT*sizeof(uint16_t));

    for (uint8_t i = 0; i < VGA_WIDTH; i++) {
        for (uint8_t j = 0; j < VGA_HEIGHT; j++) {
            vga_put_char(19, color, i, j); // 19 = double exclamation mark
        }
    }
}

static void print_segment_selector_error(unsigned long error) {
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
    if (eflags & IA_32_EFL_SYSTEM_NESTED_TASK) kprintf("NF ");
    if (eflags & IA_32_EFL_SYSTEM_RESUME) kprintf("RF ");
    if (eflags & IA_32_EFL_SYSTEM_VM8086) kprintf("VF ");
    if (eflags & IA_32_EFL_SYSTEM_ALIGN_CHECK) kprintf("ACF ");
    if (eflags & IA_32_EFL_VIRT_INTER) kprintf("VIF ");
    if (eflags & IA_32_EFL_VIRT_INTER_PEND) kprintf("VIP ");
    if (eflags & IA_32_EFL_CPUID) kprintf("ID ");
}

static inline void print_interr_frame(struct interr_frame * interr_frame) {
    kprintf("EIP:\t%p\n", interr_frame->ip);
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

__attribute__((interrupt, no_caller_saved_registers)) static void interr_divide_error(struct interr_frame * interrupt_frame) {
    kprintf("\n\n#### ISR: FPE caught! ####\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_debug_trap(struct interr_frame * interrupt_frame) {
    kprintf("\n\n#### ISR: DEBUG caught! ####\n\n");
    //panic("Debug");
}

#define NMI_RAM_ABORT " #### ISR: PANIC - RAM ERROR - NMI PARITY; HALTING #### "
#define NMI_CHANNEL_ABORT " #### ISR: PANIC - BUS ERROR - NMI CHANNEL; HALTING #### "
#define NMI_WATCHDOG_ABORT " #### ISR: PANIC - WATCHDOG TIMER RAN OUT; HALTING #### "
__attribute__((interrupt, no_caller_saved_registers)) static void interr_nmi(struct interr_frame * interrupt_frame) {
    uint8_t portA = inb(NMI_INTERR_CONTROL_PORT_A);
    uint8_t portB = inb(NMI_INTERR_CONTROL_PORT_B);
    if (portB & NMI_CONTROL_B_PARITY_CHECK || portB & NMI_CONTROL_B_CHANNEL_CHECK) {
        clear_screen_fatal();
        if (portB & NMI_CONTROL_B_PARITY_CHECK) {
            vga_x = VGA_WIDTH/2 - (sizeof(NMI_RAM_ABORT)-1)/2, vga_y = VGA_HEIGHT/2;
            kprintf(NMI_RAM_ABORT);
        } else {
            vga_x = VGA_WIDTH/2 - (sizeof(NMI_CHANNEL_ABORT)-1)/2, vga_y = VGA_HEIGHT/2;
            kprintf(NMI_CHANNEL_ABORT);
        }
        
        asm volatile ("cli; hlt;");
        __builtin_unreachable();
    }
    if (portA & NMI_CONTROL_A_WATCHDOG_TIMER) {
        vga_x = VGA_WIDTH/2 - (sizeof(NMI_WATCHDOG_ABORT)-1)/2, vga_y = VGA_HEIGHT/2;
        kprintf(NMI_WATCHDOG_ABORT);
        asm volatile ("cli; hlt;");
        __builtin_unreachable();
    }

    kprintf("\n\n#### ISR: NMI caught! ####\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_breakpoint(struct interr_frame * interrupt_frame) {
    kprintf("\n\n#### ISR: Breakpoint caught! ####\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_overflow(struct interr_frame * interrupt_frame) {
    kprintf("\n\n#### ISR: Integer overflow (INTO) caught! ####\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_bound_range_ex(struct interr_frame * interrupt_frame) {
    kprintf("\n\n#### ISR: Bound index outside of range! ####\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_invalid_opcode(struct interr_frame * interrupt_frame) {
    kprintf("\n\n#### ISR: Tried to execute invalid opcode at %p! ####\n\n", interrupt_frame->ip);
    print_interr_frame(interrupt_frame);

    scheduler_print_process(current_process);

    if (current_process->pid == 0) {
        panic("Kernel task cannot be recovered from invalid instruction exception");
        __builtin_unreachable();
    } else if (current_process->pid == 1) {
        panic("Attempted to kill init!");
        __builtin_unreachable();
    } else {
        kprintf("Terminating process id %lu\n", current_process->pid);
        scheduler_print_process(current_process);
        current_thread->status = SCHED_CLEANUP;
    }    
    
    interrupt_frame->ip = kernel_idle;
    interrupt_frame->cs = GDT_KERNEL_CODE << 3;
    interrupt_frame->ss = GDT_KERNEL_DATA << 3;
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_dev_not_avail(struct interr_frame * interrupt_frame) {
    kprintf("\n\n#### ISR: FPU Coprocessor not present or not ready! ####\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_double_fault_abort(struct interr_frame * interrupt_frame, unsigned long error) {
    //uint8_t old_tty_color = vga_get_color();
    //tty_clear();
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_RED);

    kprintf("#### ISR: CRITICAL - CAUGHT A DOUBLE FAULT! ####\n");
    print_interr_frame(interrupt_frame);
    scheduler_print_process(current_process);

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
    kprintf("\n\n#### ISR: FPU coprocessor memory segment overran! ####\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_invalid_tss(struct interr_frame * interrupt_frame, unsigned long error) {
    uint8_t old_tty_color = vga_get_color();
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);

    kprintf("\n\n#### ISR: TASK SWITCH ENCOUTERED INVALID TSS ENTRY! ####\n");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);

    vga_set_color(old_tty_color&0x0F, (old_tty_color&0xF0) >> 4);
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_segment_not_present(struct interr_frame * interrupt_frame, unsigned long error) {
    uint8_t old_tty_color = vga_get_color();
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);

    kprintf("\n\n#### ISR: REFERENCED MEMORY SEGMENT IS NOT PRESENT! ####\n");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);

    vga_set_color(old_tty_color&0x0F, (old_tty_color&0xF0) >> 4);
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_stack_segment_fault(struct interr_frame * interrupt_frame, unsigned long error) {
    uint8_t old_tty_color = vga_get_color();
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);

    kprintf("\n\n#### ISR: REFERENCE STACK ADDRESS OUTSIDE STACK SEGMENT! ####\n");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);

    vga_set_color(old_tty_color&0x0F, (old_tty_color&0xF0) >> 4);
}
extern struct idt_gate * idt_descriptor_entries;
__attribute__((interrupt, no_caller_saved_registers)) static void interr_general_protection(struct interr_frame * interrupt_frame, unsigned long error) {
    uint8_t old_tty_color = vga_get_color();
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);

    kprintf("#### ISR: Segmentation fault - Protection violation! ####\n");
    kprintf("SEL ERR:\t");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);

    scheduler_print_process(current_process);

    if (current_process->pid == 0) {
        panic("Kernel task cannot be recovered from a segmentation fault");
        __builtin_unreachable();
    } else if (current_process->pid == 1) {
        panic("Attempted to kill init!");
        __builtin_unreachable();
    } else {
        kprintf("Terminating process id %lu\n", current_process->pid);
        current_thread->status = SCHED_CLEANUP;
    }
    vga_set_color(old_tty_color&0x0F, (old_tty_color&0xF0) >> 4);
    
    interrupt_frame->ip = kernel_idle;
    interrupt_frame->cs = GDT_KERNEL_CODE << 3;
    interrupt_frame->ss = GDT_KERNEL_DATA << 3;

}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_page_fault(struct interr_frame * interrupt_frame, unsigned long error) {
    void * fault_address; // linear address
    asm volatile ("movl %%cr2, %0":"=R"(fault_address));

    if (!(error & 1)) { // caused by non-present page, see intel sdm 3A 5-55
        if (fault_address >= MEMDISKS_BASE && fault_address < MEMDISKS_BASE + MEMDISK_LIMIT_KERNEL * DEFAULT_MEMDISK_SIZE && (interrupt_frame->cs & 3) == 0) { // assuming the user cannot read memdisks themselves
            spinlock_acquire(&memdisk_lock);
            if (memdisks[GET_MEMDISK_IDX(fault_address)].used && memdisks[GET_MEMDISK_IDX(fault_address)].is_allocated) {
                paging_add_page(fault_address, PTE_PDE_PAGE_WRITABLE);
                flush_tlb_entry(fault_address);
                memset(fault_address, 0, PAGE_SIZE_NO_PAE);
                spinlock_release(&memdisk_lock);
                return;
            } else 
                spinlock_release(&memdisk_lock);
        } else { // overcommitment
            if (fault_address >= PROGRAM_HEAP_VADDR && fault_address < PROGRAM_HEAP_VADDR + PROGRAM_HEAP_SIZE) {
                paging_add_page(fault_address, PTE_PDE_PAGE_USER_ACCESS | PTE_PDE_PAGE_WRITABLE);
                return;
            }
            // unfortunately due to the kernel's usage pattern (cli, then kalloc), this would never trigger and we'd get a triple fault
            // else if (fault_address >= KERNEL_HEAP_BASE && fault_address <= KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE) {
            //    paging_add_page(fault_address, PTE_PDE_PAGE_WRITABLE);
            //    flush_tlb_entry(fault_address);
            //    return;
            //}
        } 
    }
    
    uint8_t old_tty_color = vga_get_color();
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);

    kprintf("\n\n#### ISR: Segmentation fault - Invalid memory reference! ####\nTried to reference address %p\n", fault_address);
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);

    scheduler_print_process(current_process);

    if (current_process->pid == 0) {
        panic("Kernel task cannot be recovered from a segmentation fault");
        __builtin_unreachable();
    } else if (current_process->pid == 1) {
        panic("Attempted to kill init!");
        __builtin_unreachable();
    } else {
        kprintf("Terminating process...\n");
        current_thread->status = SCHED_CLEANUP;
    }
    vga_set_color(old_tty_color&0x0F, (old_tty_color&0xF0) >> 4);
    
    interrupt_frame->ip = kernel_idle;
    interrupt_frame->cs = GDT_KERNEL_CODE << 3;
    interrupt_frame->ss = GDT_KERNEL_DATA << 3;
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_x87_float_error(struct interr_frame * interrupt_frame, unsigned long error) {
    kprintf("\n\n#### ISR: x87 FPE! ####\n\n");
}
__attribute__((interrupt, no_caller_saved_registers)) static void interr_alignment_check(struct interr_frame * interrupt_frame, unsigned long error) {
    kprintf("\n\n#### ISR: Caught unaligned memory access! ####\n");
    print_segment_selector_error(error);
    print_interr_frame(interrupt_frame);
}

#define CHECK_MACHINE_ERROR " #### ISR: PANIC - INTERNAL HW ERROR - CHECK MACHINE; HALTING #### "
__attribute__((interrupt, no_caller_saved_registers)) static void interr_machine_check_abort(struct interr_frame * interrupt_frame, unsigned long error) {
    clear_screen_fatal();

    vga_x = VGA_WIDTH/2 - (sizeof(CHECK_MACHINE_ERROR)-1)/2, vga_y = VGA_HEIGHT/2;
    
    kprintf(CHECK_MACHINE_ERROR);
    
    asm volatile("cli; hlt;");
}
//__attribute__((interrupt, no_caller_saved_registers)) void interr_simd_fpe(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_virtualization_exception(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_control_prot_exception(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_hypervisor_injection_exception(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_vmm_communication_exception(struct interr_frame * interrupt_frame);
//__attribute__((interrupt, no_caller_saved_registers)) void interr_security_exception(struct interr_frame * interrupt_frame);


__attribute__((interrupt, no_caller_saved_registers)) void general_fault_handler_error(struct interr_frame * interrupt_frame, unsigned long error) { // ul to shut clang
    uint8_t old_tty_color = vga_get_color();
    // tty_clear();
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_RED);

    kprintf("PANIC: UNREGISTERED CPU FAULT, ERR CODE %lx\n", error);
    
    vga_set_color(old_tty_color&0x0F, (old_tty_color&0xF0) >> 4);
}

__attribute__((interrupt, no_caller_saved_registers)) void general_fault_handler_no_error(struct interr_frame * interrupt_frame) { // ul to shut clang
    uint8_t old_tty_color = vga_get_color();
    // tty_clear();
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_RED);

    kprintf("PANIC: UNREGISTERED CPU FAULT, ERR CODE\n");
    
    vga_set_color(old_tty_color&0x0F, (old_tty_color&0xF0) >> 4);
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
    [INT_FAULT_X87_FLOAT_ERROR] = interr_x87_float_error,
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

void pic_send_eoi(uint8_t irq) {
    uint16_t port;
    if (irq < 8) port = PIC_M_COMM_ADDR;
    else {
        outb(PIC_M_COMM_ADDR, PIC_OCW2_EOI | PIC_OCW2_SPECIFIC | PIC_INTERR_CASCADE);
        irq -= 8;
        port = PIC_S_COMM_ADDR;
    }

    outb(port, PIC_OCW2_EOI | PIC_OCW2_SPECIFIC | (irq & 7));
}


__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_default(struct interr_frame * interrupt_frame) {
    outb(PIC_M_COMM_ADDR, PIC_OCW2_EOI);
    outb(PIC_S_COMM_ADDR, PIC_OCW2_EOI);
}

__attribute__((naked, no_caller_saved_registers)) void interr_pic_pit(struct interr_frame * interrupt_frame) {
    asm volatile(
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
    keyboard_driver(1);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_mouse(struct interr_frame * interrupt_frame) {
    kprintf("Got called");
    inb(PS2_DATA_PORT);
    pic_send_eoi(PIC_INTERR_PS2_MOUSE);
}

extern void com_recv_byte(unsigned char com);
__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_com2(struct interr_frame * interrupt_frame) {
    com_recv_byte(1);
    pic_send_eoi(PIC_INTERR_COM2);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_com1(struct interr_frame * interrupt_frame) {
    com_recv_byte(0);
    pic_send_eoi(PIC_INTERR_COM1);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_lpt2(struct interr_frame * interrupt_frame) {
    pic_send_eoi(PIC_INTERR_LPT2);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_floppy(struct interr_frame * interrupt_frame) {
    pic_send_eoi(PIC_INTERR_FLOPPY);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_lpt1(struct interr_frame * interrupt_frame) {
    if (pic_is_spurious(PIC_INTERR_LPT1)) return;

    pic_send_eoi(PIC_INTERR_LPT1);
}

__attribute__((interrupt, no_caller_saved_registers)) void interr_cmos_rtc(struct interr_frame * interrupt_frame) {
    enum rtc_interrupt_bitmasks called_ints = rtc_get_last_interrupt_type();
    pic_send_eoi(PIC_INTERR_CMOS_RTC);

    if (called_ints & RTC_INT_PERIODIC) {
        uptime_clicks ++;
    }
    if (called_ints & RTC_INT_ALARM) {
        kprintf("Recieved RTC alarm interrupt\n");
    }
    if (called_ints & RTC_INT_UPDATE_ENDED) {
        system_time_sec ++;
    }
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
