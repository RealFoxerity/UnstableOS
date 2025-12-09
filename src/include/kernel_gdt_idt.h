#ifndef KERNEL_GDT_IDT_H
#define KERNEL_GDT_IDT_H
#include <stdint.h>
#include "kernel_interrupts.h"

#define KERNEL_TS_STACK_SIZE 0x1000
extern void * kernel_ts_stack_top; // stack used in interrupts when TSS is involved

#define IDT_INTERR_VECTOR_COUNT 256 // max

#define GDT_KERNEL_CODE 1 //    all hardcoded, don't change
#define GDT_KERNEL_DATA 2 
#define GDT_USER_CODE 3
#define GDT_USER_DATA 4
#define GDT_KERNEL_TSS 5
#define GDT_USER_TSS 6

enum gdt_segment_acc_byte {
    GDT_SEG_ACC_ACCESSED = 1, // if unset and cpu tries to set and the segment is inside read only, page fault is raised
    GDT_SEG_ACC_RW = 2, // for code controls read, WRITE is never allowed; for data controls write, READ is always allowed
    GDT_SEG_ACC_DC = 4, // for data sets whether segment grows up (not set), if set grows down, offset has to be greater than limit
                        // if set for code segments, allows long jumps from equal or LOWER privilage levels (ring 3 can jump to ring 0, ring 0 can't jump to ring 3), otherwise only equal
    GDT_SEG_ACC_EXEC = 8, // 1 = code segment, 0 = data segment
    GDT_SEG_ACC_NOT_SYS_SEG = 0x10, // if set means segment is not a system segment
    GDT_SEG_ACC_PRIV_R1 = 0x20, // R0 is nothing
    GDT_SEG_ACC_PRIV_R2 = 0x40,
    GDT_SEG_ACC_PRIV_R3 = 0x60,
    GDT_SEG_ACC_PRESENT = 0x80,
};

enum gdt_sys_segment_acc_byte { // tss/ldt
    GDT_SYS_SEG_ACC_TYPE_16TSS_AVAIL = 1,
    GDT_SYS_SEG_ACC_TYPE_LDT = 2,
    GDT_SYS_SEG_ACC_TYPE_16TSS_BUSY = 3,
    GDT_SYS_SEG_ACC_TYPE_32TSS_AVAIL = 9,
    GDT_SYS_SEG_ACC_TYPE_32TSS_BUSY = 0xB,
    
    GDT_SYS_SEG_ACC_TYPE_64_LDT = 2, // works only in long mode
    GDT_SYS_SEG_ACC_TYPE_64_64TSS_AVAIL = 9, // works only in long mode
    GDT_SYS_SEG_ACC_TYPE_64_64TSS_BUSY = 0xB, // works only in long mode
    
    GDT_SYS_SEG_ACC_NOT_SYS_SEG = 0x10, // if set means segment is not a system segment
    GDT_SYS_SEG_ACC_PRIV_R1 = 0x20,
    GDT_SYS_SEG_ACC_PRIV_R2 = 0x40,
    GDT_SYS_SEG_ACC_PRIV_R3 = 0x60,
    GDT_SYS_SEG_ACC_PRESENT = 0x80,
};

enum gdt_segment_flags {
    GDT_SEG_FL_64BIT = 2, // called the long mode flag; if the case, 32bit has to be 0
    GDT_SEG_FL_32BIT = 4, // called the DB; otherwise 16 bit
    GDT_SEG_FL_PAGED_LIMIT = 8, // granuality, if set the limit is in pages
};


enum idt_gate_flags {
    IDT_GATE_FL_GATE_TYPE_TASK = 0x5, // offset unused, set to 0
    IDT_GATE_FL_GATE_TYPE_INTERRUPT = 0x6, // for general functions, reentrant
    IDT_GATE_FL_GATE_TYPE_TRAP = 0x7, // exceptions & stuff

    IDT_GATE_FL_GATE_TYPE_32INT = 0xe,
    IDT_GATE_FL_GATE_TYPE_32TRAP = 0xf,

    IDT_GATE_FL_PRESENT = 0x80,

    IDT_GATE_FL_INT_PRIV_R1 = 0x20, // ring level required to run this interrupt with INT instruction, R0 = 0
    IDT_GATE_FL_INT_PRIV_R2 = 0x40,
    IDT_GATE_FL_INT_PRIV_R3 = 0x60
};

struct tss_segment {  // espX, ssX = values for ring X
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldtr;
    uint16_t trap; // 1 bit, if set causes the cpu to raise a debug exception when task switch occurs
    uint16_t iopb; //  I/O Map Base Address Field. Contains a 16-bit offset from the base of the TSS to the I/O Permission Bit Map.
    //uint32_t ssp; // shadow stack pointer
} __attribute__((packed)); // attribute should not be needed considering the word size, but to be sure

struct idt_gate { // reference, not used
    uint16_t offset_lower;
    uint16_t segment_selector;
    uint8_t __zero;
    uint8_t flags;
    uint16_t offset_upper;
} __attribute__((packed));

struct dt_descriptor {
    uint16_t size;
    uint32_t linear_address; // virtual + segment base????? not using segmentation so just virtual
} __attribute__((packed));

void construct_descriptor_tables(); // Warning: enables CPU interrupts (exception/faults/traps)
void tss_set_stack(unsigned long * new_esp);
#endif