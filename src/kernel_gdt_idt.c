#include "include/kernel_interrupts.h"
#include "include/lowlevel.h"
#include "include/kernel.h"
#include "include/mm/kernel_memory.h"
#include "../libc/src/include/string.h"
#include "include/kernel_gdt_idt.h"
#include <stdint.h>
#include <stddef.h>


#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wvoid-pointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"


static uint64_t gdt_generate_descriptor(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) { // limit is actually 20 bytes, flags are actually 4 bits
    //if (flags & GDT_SEG_FL_PAGED_LIMIT) limit >>= 12; // granuality means the adresses are in page indices
    uint64_t descriptor = 0;
    
    descriptor |= ((uint64_t)(base & 0xFF000000) << 32);
    descriptor |= ((uint64_t)(base & 0x00FFFFFF) << 16);    
    descriptor |= ((uint64_t)(limit & 0x0000FFFF));
    descriptor |= ((uint64_t)(limit & 0x000F0000) << 32);
    
    descriptor |= ((uint64_t)access << 40);
    
    descriptor |= ((uint64_t)(flags & 0x0F) << 52);
    
    return descriptor;
}

static inline struct idt_gate idt_generate_descriptor(uint32_t offset, uint16_t code_segment_dt_idx, uint8_t use_ldt, uint8_t selector_priv_level, uint8_t flags) {
    struct idt_gate descriptor = {0};
    
    descriptor.segment_selector = (code_segment_dt_idx<<3 | (use_ldt&1)<<2 | (selector_priv_level&3));
    
    descriptor.flags = flags;

    descriptor.offset_lower = offset & 0x0000FFFF;
    descriptor.offset_upper = (offset & 0xFFFF0000) >> 16;
    return descriptor;
}

struct tss_segment tss __attribute__((aligned(0x1000))) = {0};

static void tss_generate() { // stub for software task switching
    tss.esp0 = (unsigned long)kernel_ts_stack_top;
    tss.ss0 = GDT_KERNEL_DATA<<3;
    
    //tss.cs = GDT_KERNEL_CODE<<3;
    //tss.ds = tss.es = tss.fs = tss.gs = tss.ss = tss.ss0;
    //tss.trap = 1;
    
    tss.iopb = sizeof(struct tss_segment); // not using it, this is just a good default value
}

void tss_set_stack(unsigned long * new_esp) {
    tss.esp0 = (unsigned long) new_esp;
}

#define GDT_ENTRIES 6 // NULL descriptor (used as the gdtr store), kernel code, kernel data, user code, user data, tss

static uint64_t * gdt_descriptor_entries = NULL;
static struct idt_gate idt_descriptor_entries[IDT_INTERR_VECTOR_COUNT] __attribute__((aligned(0x1000))) = {0};
static struct dt_descriptor idtr = {0};
void * kernel_ts_stack_top;

void construct_descriptor_tables() {
    asm volatile ("cli");

    kernel_ts_stack_top = kalloc(KERNEL_TS_STACK_SIZE);
    kernel_ts_stack_top += KERNEL_TS_STACK_SIZE;
    
    if (kernel_ts_stack_top == NULL) panic("Not enough memory for task switch stack!\n");

    gdt_descriptor_entries = kalloc(sizeof(uint64_t) * GDT_ENTRIES); 
    //idt_descriptor_entries = kalloc(sizeof(struct idt_gate) * IDT_INTERR_VECTOR_COUNT);

    if (gdt_descriptor_entries == NULL /* || idt_descriptor_entries == NULL*/) panic("Could not allocate enough memory for description tables!");

    memset(gdt_descriptor_entries, 0, sizeof(uint64_t)*GDT_ENTRIES);
    memset(idt_descriptor_entries, 0, sizeof(struct idt_gate)*IDT_INTERR_VECTOR_COUNT);

    idtr.linear_address = (uint32_t)idt_descriptor_entries;
    idtr.size = sizeof(struct idt_gate)*IDT_INTERR_VECTOR_COUNT - 1;

    *(struct dt_descriptor*)gdt_descriptor_entries = (struct dt_descriptor) { // using the NULL descriptor for GDTR, not really needed but meh
        .size = sizeof(uint64_t)*GDT_ENTRIES - 1, // size is always subracted by one, no clue why
        .linear_address = (uint32_t)gdt_descriptor_entries
    };



    // IDT
    uint32_t ptr = 0;
    for (int i = 0; i < RES_INTERR_EXCEPTION_COUNT; i++) {
        ptr = (uint32_t)cpu_interr_handlers[i];
        if (ptr == 0) {
            if (i >= RES_INTERR_EXCEPTION_COUNT) ptr = (uint32_t)general_fault_handler_no_error;
            else
            ptr = reserved_idt_interr_has_error[i]?(uint32_t)general_fault_handler_error:(uint32_t)general_fault_handler_no_error;
        }
        //idt_descriptor_entries[i] = idt_generate_descriptor(ptr, GDT_KERNEL_CODE, 0, 0, IDT_GATE_FL_PRESENT | IDT_GATE_FL_GATE_TYPE_32TRAP);
        
        idt_descriptor_entries[i] = idt_generate_descriptor(ptr, GDT_KERNEL_CODE, 0, 0, IDT_GATE_FL_PRESENT | IDT_GATE_FL_GATE_TYPE_32INT); 
        // should be traps, but all defined exceptions take too long to manage processes for it to be useful
    }

    //for (int i = RES_INTERR_EXCEPTION_COUNT; i < IDT_INTERR_VECTOR_COUNT; i++) { // so turns out i shouldn't prefill the table...
    //    idt_descriptor_entries[i] = idt_generate_descriptor((uint32_t)general_fault_handler_no_error, GDT_KERNEL_CODE, 0, 0, IDT_GATE_FL_PRESENT | IDT_GATE_FL_GATE_TYPE_32TRAP); // | IDT_GATE_FL_INT_PRIV_R3);
    //}
    
    for (int i = IDT_PIC_INTERR_START; i < IDT_PIC_INTERR_START+PIC_INTERR_COUNT; i++) {
        ptr = (uint32_t)pic_interr_handlers[i-IDT_PIC_INTERR_START];
        if (ptr) {
            idt_descriptor_entries[i] = idt_generate_descriptor(ptr, GDT_KERNEL_CODE, 0, 0, IDT_GATE_FL_PRESENT | IDT_GATE_FL_GATE_TYPE_32INT); // | IDT_GATE_FL_INT_PRIV_R3);
        }
        else idt_descriptor_entries[i] = idt_generate_descriptor((uint32_t)interr_pic_default, GDT_KERNEL_CODE, 0, 0, IDT_GATE_FL_PRESENT | IDT_GATE_FL_GATE_TYPE_32INT); // | IDT_GATE_FL_INT_PRIV_R3);

    }

    idt_descriptor_entries[SYSCALL_INTERR] = idt_generate_descriptor((uint32_t)interr_syscall, GDT_KERNEL_CODE, 0, 0, IDT_GATE_FL_PRESENT | IDT_GATE_FL_GATE_TYPE_32INT | IDT_GATE_FL_INT_PRIV_R3);

    pic_setup(IDT_PIC_INTERR_START, IDT_PIC_INTERR_START+8);

    //idt_descriptor_entries[INT_ABORT_DOUBLE_FAULT] = idt_descriptor_entries[INT_ABORT_MACHINE_CHECK]; // test

    asm volatile (
        "lidt %0\n\t"
        "sti"
        :: "m"(idtr)
    );


    
    //  GDT

    gdt_descriptor_entries[GDT_KERNEL_CODE] = gdt_generate_descriptor(0,0x000FFFFF, GDT_SEG_ACC_RW | GDT_SEG_ACC_EXEC | GDT_SEG_ACC_NOT_SYS_SEG | GDT_SEG_ACC_PRESENT, GDT_SEG_FL_32BIT| GDT_SEG_FL_PAGED_LIMIT); // kernel code
    gdt_descriptor_entries[GDT_KERNEL_DATA] = gdt_generate_descriptor(0,0x000FFFFF, GDT_SEG_ACC_RW | GDT_SEG_ACC_NOT_SYS_SEG | GDT_SEG_ACC_PRESENT, GDT_SEG_FL_32BIT| GDT_SEG_FL_PAGED_LIMIT); // kernel data

    gdt_descriptor_entries[GDT_USER_CODE] = gdt_generate_descriptor(0,0x000FFFFF, GDT_SEG_ACC_RW | GDT_SEG_ACC_EXEC | GDT_SEG_ACC_NOT_SYS_SEG | GDT_SEG_ACC_PRIV_R3 | GDT_SEG_ACC_PRESENT, GDT_SEG_FL_32BIT| GDT_SEG_FL_PAGED_LIMIT); // usermode code
    gdt_descriptor_entries[GDT_USER_DATA] = gdt_generate_descriptor(0,0x000FFFFF, GDT_SEG_ACC_RW | GDT_SEG_ACC_NOT_SYS_SEG | GDT_SEG_ACC_PRIV_R3 | GDT_SEG_ACC_PRESENT, GDT_SEG_FL_32BIT| GDT_SEG_FL_PAGED_LIMIT); // usermode data

    tss_generate();
    gdt_descriptor_entries[GDT_KERNEL_TSS] = gdt_generate_descriptor((uint32_t)(&tss), sizeof(struct tss_segment)-1, GDT_SEG_ACC_PRESENT | GDT_SEG_ACC_ACCESSED | GDT_SYS_SEG_ACC_TYPE_32TSS_AVAIL, 0); // note: for TSS flags are always zero, not even GDT_SEG_FL_32BIT

    struct dt_descriptor gdtr = *(struct dt_descriptor *)gdt_descriptor_entries;

    asm volatile (
        "lgdt %0\n\t"
        "ljmp $0x08, $thunk\n\t"
        "thunk:\n\t"
        "mov $0x10, %%ax\n\t" // 0x10 is the offset into gdt where the data segment is (3rd)
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        :: "m"(gdtr)
    );

    asm volatile ( // enabling of TSS, a) wouldn't make sense without interrupts, b) if an error occurs, we'd get triple fault without interrupts
        "mov $0x28, %%ax\n\n" // Lower 3 bits actually mean at what ring it's possible to call a task switch
        "ltr %%ax":::"eax"
    );

    // now to protect the IDT
    paging_change_flags(idt_descriptor_entries, sizeof(struct idt_gate) * IDT_INTERR_VECTOR_COUNT, 0);
}