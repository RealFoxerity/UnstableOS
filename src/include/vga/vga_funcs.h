#ifndef VGA_FUNCS_H
#define VGA_FUNCS_H

enum vga_modes {
    CHAINED,
    UNCHAINED,
    MODE12,
};

extern enum vga_modes current_vga_mode;


/****************************
* disable interrupts before calling any vga setup functions! 
*****************************/

void vga_init_graphics();
void vga_reset_sequencer();

void vga_disable_scan();
void vga_enable_scan();

void vga_set_palette_entry(char idx, unsigned char rgb8); // overwrites DAC!
void vga_generate_256colors(); // regenerate linear color space

enum vga_addressing_modes {
    VGA_AM_BYTE,
    VGA_AM_WORD,
    VGA_AM_DWORD
};
void vga_set_addressing_mode(enum vga_addressing_modes mode);

#include <stdint.h>
void vga_wreg(uint16_t data_reg, uint8_t index, uint8_t data);
void vga_wrattr(uint8_t index, uint8_t data);

#endif