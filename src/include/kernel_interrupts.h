#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

#include <stdint.h>

#define PIC_M_COMM_ADDR 0x20
#define PIC_M_DATA_ADDR 0x21
#define PIC_S_COMM_ADDR 0xA0
#define PIC_S_DATA_ADDR 0xA1

#define PIC_COMMAND_EOI 0x20


#define IDT_PIC_INTERR_START RES_INTERR_EXCEPTION_COUNT
#define PIC_INTERR_COUNT 16

enum pic_interrupt_requests {
    // master
    PIC_INTERR_PIT,
    PIC_INTERR_KEYBOARD,
    PIC_INTERR_CASCADE, // the interrupt between master and slave pic, never raised
    PIC_INTERR_COM2,
    PIC_INTERR_COM1,
    PIC_INTERR_LPT2,
    PIC_INTERR_FLOPPY,
    PIC_INTERR_LPT1, // can be spurious

    
    // slave
    PIC_INTERR_CMOS_RTC,
    PIC_INTERR_USER_1,
    PIC_INTERR_USER_2,
    PIC_INTERR_USER_3,
    PIC_INTERR_PS2_MOUSE,
    PIC_INTERR_COPROCESSOR,
    PIC_INTERR_PRIMARY_ATA,
    PIC_INTERR_SECONDARY_ATA,  // can be spurious
};

#define NMI_INTERR_CONTROL_PORT_A 0x92
#define NMI_INTERR_CONTROL_PORT_B 0x61

enum nmi_control_port_a {
    NMI_CONTROL_A_ALT_HOT_RESET = 1,
    NMI_CONTROL_A_ALT_GATE_20 = 2,
    NMI_CONTROL_A_SECURITY_LOCK = 8,
    NMI_CONTROL_A_WATCHDOG_TIMER = 16, //!!
    NMI_CONTROL_A_HDD_2_DRIVE_ACT = 64,
    NMI_CONTROL_A_HDD_1_DRIVE_ACT = 128,
};

enum nmi_control_port_b {
    NMI_CONTROL_B_TIMER_2_TIED_SPEAKER = 1,
    NMI_CONTROL_B_SPEAKER_DATA_ENABLE = 2,
    NMI_CONTROL_B_PARITY_CHECK_ENABLE = 4,
    NMI_CONTROL_B_CHANNEL_CHECK_ENABLE = 8,
    NMI_CONTROL_B_REFRESH_REQUEST = 16,
    NMI_CONTROL_B_TIMER_2_OUTPUT = 32,
    NMI_CONTROL_B_CHANNEL_CHECK = 64, // !! failure on the bus
    NMI_CONTROL_B_PARITY_CHECK = 128 // !! memory read/write error
};

#define RES_INTERR_EXCEPTION_COUNT 0x20 // including reserved
//#define RES_INTERR_VECTOR_COUNT 0x20 // only the used, for array init
enum reserved_idt_interrupts {
    INT_FAULT_DIVIDE_ERROR,
    INT_DEBUG_EXCEPTION, // genuinelly just a debug breakpoint, can be both trap and fault, see the debug register
    INT_INT_NMI_INTERRUPT,
    INT_TRAP_BREAKPOINT,      // normal breakpoint - int3
    INT_TRAP_OVERFLOW,        // INTO instruction called - just enter an interrupt if overflow flag = 1
    INT_FAULT_BOUND_RANGE_EXCEEDED, // bound is an instruction checking whether value is between 2 other values, raises interr if not
    INT_FAULT_INVALID_OP,
    INT_FAULT_DEV_NOT_READY,            // coprocessor (fpu) not available/does not exist or FWAIT/WAIT
    INT_ABORT_DOUBLE_FAULT,             // has error code, zero, 1 more layer of unhandled fault and cpu self-resets
    INT_FAULT_COPROCESSOR_SEG_OVERRUN,  // segment overrun on coprocessor
    INT_FAULT_INVALID_TSS,              // has error code, HW task switch ran into invalid segment note: will not be using HW TS anyway
    INT_FAULT_SEGMENT_NOT_PRESENT,      // has error code, referenced segment doesn't have the present bit set, see gdt
    INT_FAULT_STACK_SEGMENT_FAULT,      // has error code
    INT_FAULT_GENERAL_PROTECTION,       // has error code
    INT_FAULT_PAGE_FAULT,               // has error code, accessed page is not mapped
    INT_RES_RESERVED,
    INT_FAULT_X87_FLOAT_ERROR,          // FWAIT/WAIT/any waiting floating instruction called AND CR0.NE(numeric error) = 1 AND unmasked x87 exception is pending
    INT_FAULT_ALIGNMENT_CHECK,          // has error code, 0, when flag for memory reference alignment is set and an unaligned reference is performed
    INT_ABORT_MACHINE_CHECK,
    INT_FAULT_SIMD_FPE,                 //  when an unmasked 128-bit media floating-point exception occurs and the CR4.OSXMMEXCPT bit is set to 1, otherwise INVALID_OP
    INT_FAULT_VIRTUALIZATION_EXCEPTION,
    INT_FAULT_CONTROL_PROT_EXCEPTION,   // has error code
    // 0x16 - 0x1b reserved for future cpu vectors
    INT_FAULT_HYPERVISOR_INJECTION_EX = 0x1C,
    INT_FAULT_VMM_COMMUNICATION_EX,     // has error code
    INT_FAULT_SECURITY_EXCEPTION,       // has error code
};


extern const char reserved_idt_interr_has_error[RES_INTERR_EXCEPTION_COUNT];
extern const void * cpu_interr_handlers[RES_INTERR_EXCEPTION_COUNT];
extern const void * pic_interr_handlers[PIC_INTERR_COUNT];
struct interr_frame {
    void * ip; // instruction pointer
    unsigned long cs; // code segment
    unsigned long flags; // check flags, e.g. LT IZ ...
    void * sp; // stack pointer
    unsigned long ss; // stack segment
} __attribute__((packed));

__attribute__((interrupt, no_caller_saved_registers)) void general_fault_handler_error(struct interr_frame * interrupt_frame, unsigned long error);
__attribute__((interrupt, no_caller_saved_registers)) void general_fault_handler_no_error(struct interr_frame * interrupt_frame);
__attribute__((interrupt, no_caller_saved_registers)) void interr_pic_default(struct interr_frame * interrupt_frame);

__attribute__((naked, no_caller_saved_registers)) void interr_syscall(struct interr_frame * interrupt_frame);

void pic_setup(uint8_t lower_idt_off, uint8_t higher_idt_off); // warning, disables pic interrupts
void pic_mask_irq(uint8_t irq_num);
void pic_unmask_irq(uint8_t irq_num);


void disable_interrupts();
void enable_interrupts();
#endif