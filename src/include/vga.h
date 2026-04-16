#ifndef VGA_H
#define VGA_H
#include <stdint.h>

#define VGA_PAGE_ADDR ((unsigned char*)0xA0000)

#define VGA_INPUT_STATUS_1_REGISTER 0x3DA 

#define VGA_CRTC_IDX_REG 0x3D4
#define VGA_CRTC_DATA_REG 0x3D5

#define VGA_CRTC_MODE_CONTROL 0x17

#define VGA_SEQ_IDX_REG 0x3C4
#define VGA_SEQ_DATA_REG 0x3C5

#define VGA_DAC_BIT_MASK_REG 0x3C6 // which bits are used for color
#define VGA_DAC_WRMODE_REG 0x3C8
#define VGA_DAC_RDMODE_REG 0x3C7
#define VGA_DAC_STATE_REG 0x3C7
#define VGA_DAC_DATA_REG 0x3C9

#define VGA_MISC_OUT_REG_WR 0x3C2

#define VGA_GC_IDX_REG 0x3CE
#define VGA_GC_DATA_REG 0x3CF

#define VGA_GC_5_SHIFT256 0b01000000

#define VGA_AC_REG 0x3C0
#define VGA_AC_REG_READ 0x3C1

#define VGA_SEQ_4_CHAIN4 0x8

#include <stdint.h>
#include <stddef.h>

#define VGA_RGB_TO_RGB8(r, g, b) (\
    (((r) & 0b11100000) >> 0) |\
    (((g) & 0b11100000) >> 3) |\
    (((b) & 0b11000000) >> 6)\
)

#define VGA_RGB32_TO_RGB8(color) (VGA_RGB_TO_RGB8((color) >> 24, (color) >> 16, (color) >> 8))

#include "vga/vga_modelines.h"
#include "vga/vga_funcs.h"

// 300x200 256 colors, fast
void vga_set_mode_13();

// 320x240 256 colors, slow
void vga_set_mode_X();

// 360x240 256 colors, slow
void vga_set_mode_X_wide();

// 400x300 256 colors, fast
//void vga_set_mode_hilinear();

// 640x480 16 colors, VERY slow
void vga_set_mode_12();

// 720x480 16 colors, VERY slow
void vga_set_mode_12_wide();

extern const unsigned char vga_font8x8[2048];
extern unsigned char * vga_font8x16;

// all x/y variables are zero-based

// reads from the *shadow framebuffer*
uint32_t vga_read_pixel(unsigned int x, unsigned int y);

// to a shadow framebuffer
void vga_write_pixel_buffered(unsigned int x, unsigned int y, uint32_t color, char use_palette);

// blits/syncs part of the shadow framebuffer, inclusive
void vga_swap_region(unsigned int start_x, unsigned int end_x, unsigned int start_y, unsigned int end_y);

// clears both the shadow framebuffer and the display
// faster than vga_clear_screen_buffered followed by vga_swap_buffers
void vga_clear_screen();

void vga_fill_buffered(unsigned int start_x, unsigned int end_x, unsigned start_y, unsigned int end_y, uint32_t color, char use_palette);


//NOTE:
//QEMU has a bug that breaks scrolling and makes it show glitchy scanlines
//however these functions work normally on real hardware
//i'll try to make a patch to qemu and send it

//#define VGA_NO_HW_SHIFT
/*
define this to instead do a memcpy of all pixels
this will be CONSIDERABLY slower

durations in QEMU
mode   |   duration for HW scroll   |   duration for memcpy
12     |   ~0.1 seconds/instant     |   ~4 seconds with heavy artifacting
12_wide|   ~0.1 seconds/instant     |   ~6 seconds with heavy artifacting
13     |   not applicable           |   ~0.3 seconds
X      |   instant                  |   ~0.5 seconds with artifacting
X_wide |   instant                  |   ~0.6 seconds with artifacting

on real (tested on modern VBE capable) hardware, both are basically instanteneous

mode 13 is the only chained mode we currently support
all chained modes aren't hardware scrollable and due to that
chained modes are defaulted to memcpy
*/

// mode 13 doesn't play nice with hardware shifting, so force into 

// adjusts the start of scanline by X pixels relative to previous adjustment
void vga_hw_shift_pixels(unsigned int pixels);

#define vga_clear_region(x, y, width, height)\
	vga_copy_region(x, y, width, height, display_width, display_height)
// not safe when x and final_x overlap!
void vga_copy_region(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int final_x, unsigned int final_y);

#include "gfx.h"
extern struct gfx_funcs vga_funcs;

#endif