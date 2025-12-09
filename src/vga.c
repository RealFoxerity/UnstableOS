#include <stdint.h>
#include "include/lowlevel.h"
#include "include/vga.h"


uint8_t vga_read_attribute(uint8_t index) {
    uint32_t interr_enabled = 0;
    asm volatile (
        "pushf\n\t"
        "andl $"STR(IA_32_EFL_SYSTEM_INTER_EN) ", (%%esp)\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(interr_enabled)
    );
    inb(VGA_INPUT_STATUS_1_REGISTER); // force resets the attribute register to index phase
        
    outb(VGA_ATTRIBUTE_REGISTER_A_PORT, index);
    uint8_t out = inb(VGA_ATTRIBUTE_REGISTER_R_PORT);
    inb(VGA_INPUT_STATUS_1_REGISTER);
    
    if (interr_enabled) asm volatile ("sti");
    return out;
}

void vga_write_attribute(uint8_t index, uint8_t data) {
    uint32_t interr_enabled = 0;
    asm volatile (
        "pushf\n\t"
        "andl $"STR(IA_32_EFL_SYSTEM_INTER_EN) ", (%%esp)\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(interr_enabled)
    );
    inb(VGA_INPUT_STATUS_1_REGISTER); // force resets the attribute register to index phase

    outb(VGA_ATTRIBUTE_REGISTER_A_PORT, index);
    outb(VGA_ATTRIBUTE_REGISTER_A_PORT, data);

    if (interr_enabled) asm volatile ("sti");
}