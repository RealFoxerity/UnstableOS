#include "../include/vga.h"
#include "../include/vga/vga_funcs.h"
#include "../include/vga/vga_modelines.h"
#include "../include/kernel_spinlock.h"

extern spinlock_t vga_spinlock;
extern unsigned int vga_pixel_offset;

// linear framebuffer, 320x200 (double scanned) 256 colors
void vga_set_mode_13() {
    spinlock_acquire(&vga_spinlock);

    // we can't use 320x240 in mode 13, page is not long enough
    vga_pixels_per_address = 4;
    vga_scan_doubling = 1;
    vga_pixel_offset = 0; // mode 13 REALLY doesn't like hardware scrolling
    // the VGA CRTC reads from all planes in [some] order
    // since this mode fits into a single plane, we need to set the CRTC
    // to take all 4 planes as a single byte
    vga_set_addressing_mode(VGA_AM_DWORD);

    vga_load_timings(vga_640x400, 320);
    display_width = 320;
    display_height = 200;

    // 64KB memory region, graphics mode
    vga_wreg(VGA_GC_DATA_REG, 6, 0b00000101);

    // 256 color mode, direct write, direct read
    vga_wreg(VGA_GC_DATA_REG, 5, VGA_GC_5_SHIFT256);

    // "chained" - linear - framebuffer
    vga_wreg(VGA_SEQ_DATA_REG, 4, 0b00000110 | VGA_SEQ_4_CHAIN4);

    // 8 bit color
    vga_wrattr(0x10, 0x41);

    // chained mode uses all planes at once
    vga_wreg(VGA_SEQ_DATA_REG, 2, 0xF);

    vga_generate_256colors();

    vga_reset_sequencer();
    current_vga_mode = CHAINED;
    spinlock_release(&vga_spinlock);
}

// planar framebuffer, 320x240 (double scanned) 256 colors
void vga_set_mode_X() {
    spinlock_acquire(&vga_spinlock);

    // we can use 320x240 since mode X and 12 do planes
    vga_pixels_per_address = 4;
    vga_scan_doubling = 1;
    vga_set_addressing_mode(VGA_AM_BYTE);

    vga_load_timings(vga_640x480, 320);
    display_width = 320;
    display_height = 240;

    // 64KB memory region, graphics mode
    vga_wreg(VGA_GC_DATA_REG, 6, 0b00000101);

    // 256 color mode, direct write, direct read
    vga_wreg(VGA_GC_DATA_REG, 5, VGA_GC_5_SHIFT256);

    // 8 bit color
    vga_wrattr(0x10, 0x41);

    // "unchained" - planar - framebuffer
    vga_wreg(VGA_SEQ_DATA_REG, 4, 0b00000110);

    vga_generate_256colors();

    vga_reset_sequencer();
    current_vga_mode = UNCHAINED;
    spinlock_release(&vga_spinlock);
}

// planar framebuffer, 360x240 (double scanned) 256 colors
void vga_set_mode_X_wide() {
    spinlock_acquire(&vga_spinlock);

    vga_pixels_per_address = 4;
    vga_scan_doubling = 1;
    vga_set_addressing_mode(VGA_AM_BYTE);

    vga_load_timings(vga_720x480, 360);
    display_width = 360;
    display_height = 240;

    // 64KB memory region, graphics mode
    vga_wreg(VGA_GC_DATA_REG, 6, 0b00000101);

    // 256 color mode, direct write, direct read
    vga_wreg(VGA_GC_DATA_REG, 5, VGA_GC_5_SHIFT256);

    // 8 bit color
    vga_wrattr(0x10, 0x41);

    // "unchained" - planar - framebuffer
    vga_wreg(VGA_SEQ_DATA_REG, 4, 0b00000110);

    vga_generate_256colors();

    vga_reset_sequencer();
    current_vga_mode = UNCHAINED;
    spinlock_release(&vga_spinlock);
}

// planar framebuffer, 640x480 16 colors
void vga_set_mode_12() {
    spinlock_acquire(&vga_spinlock);

    vga_pixels_per_address = 8;
    vga_scan_doubling = 0;
    vga_set_addressing_mode(VGA_AM_BYTE);

    vga_load_timings(vga_640x480, 640);
    display_width = 640;
    display_height = 480;

    // 64KB memory region, graphics mode
    vga_wreg(VGA_GC_DATA_REG, 6, 0b00000101);

    // non-256 color mode, direct write, direct read
    vga_wreg(VGA_GC_DATA_REG, 5, 0);

    // non-8 bit color
    vga_wrattr(0x10, 0x01);

    // "unchained" - planar - framebuffer
    vga_wreg(VGA_SEQ_DATA_REG, 4, 0b00000110);

    for (int i = 0; i < 16; i++)
        vga_set_palette_entry(i, console_colors[i]);

    vga_reset_sequencer();
    current_vga_mode = MODE12;
    spinlock_release(&vga_spinlock);
}

// planar framebuffer, 720x480 16 colors
void vga_set_mode_12_wide() {
    spinlock_acquire(&vga_spinlock);

    vga_pixels_per_address = 8;
    vga_scan_doubling = 0;
    vga_set_addressing_mode(VGA_AM_BYTE);

    vga_load_timings(vga_720x480, 720);
    display_width = 720;
    display_height = 480;

    // 64KB memory region, graphics mode
    vga_wreg(VGA_GC_DATA_REG, 6, 0b00000101);

    // non-256 color mode, direct write, direct read
    vga_wreg(VGA_GC_DATA_REG, 5, 0);

    // non-8 bit color
    vga_wrattr(0x10, 0x01);

    // "unchained" - planar - framebuffer
    vga_wreg(VGA_SEQ_DATA_REG, 4, 0b00000110);

    for (int i = 0; i < 16; i++)
        vga_set_palette_entry(i, console_colors[i]);

    vga_reset_sequencer();
    current_vga_mode = MODE12;
    spinlock_release(&vga_spinlock);
}

/* like 90% of VGA cards don't have the required clock :P
// linear framebuffer, 400x300 (double scanned) 256 colors, needs 42 MHz clock
void vga_set_mode_hilinear() {
    vga_pixels_per_address = 4;
    vga_scan_doubling = 1;
    vga_set_addressing_mode(VGA_AM_BYTE);

    vga_load_timings(vga_800_600, 400);
    display_width = 400;
    display_height = 300;

    // 128KB memory region, graphics mode
    vga_wreg(VGA_GC_DATA_REG, 6, 0b00000001);

    // 256 color mode, direct write, direct read
    vga_wreg(VGA_GC_DATA_REG, 5, VGA_GC_5_SHIFT256);

    // 8 bit color
    vga_wrattr(0x10, 0x41);
    
    // "chained" - planar - framebuffer
    vga_wreg(VGA_SEQ_DATA_REG, 4, 0b00000110 | VGA_SEQ_4_CHAIN4);

    // chained mode uses all planes at once
    vga_wreg(VGA_SEQ_DATA_REG, 2, 0xF);

    vga_generate_256colors();

    vga_reset_sequencer();

    current_vga_mode = CHAINED;
}
*/