#ifndef VBE_H
#define VBE_H
#include <stdint.h>

#define VBE_LINEAR_FRAMEBUFFER_START ((void*)0x06000000)
#define VBE_LINEAR_FRAMEBUFFER_MAX_SIZE 0x01000000

#define VBE_MEMORY_MODEL_PACKED 0x04
#define VBE_MEMORY_MODEL_DIRECT 0x06
#define VBE_MEMORY_MODEL_YUV    0x07

struct VBEIBlock {
    char signature[4];
    uint16_t version;
    uint16_t oem_string[2]; // far pointer
    uint32_t capabilities;
    uint16_t video_modes[2]; // far pointer
    uint16_t total_memory_64k_blocks;
    uint16_t sw_revision;
    uint16_t vendor_string[2];
    uint16_t product_string[2];
    uint16_t product_revision[2];

    uint8_t __resv[478];
} __attribute__((packed));

struct VBE_mode_info {
    struct {
        uint16_t supported          : 1;
        uint16_t __resv             : 1;
        uint16_t tty_output         : 1;
        uint16_t color_mode         : 1;
        uint16_t gfx_mode           : 1;
        uint16_t vga_incom          : 1;
        uint16_t vga_incom_window   : 1;
        uint16_t linear_framebuffer : 1;
    } attributes;
    uint8_t  window_a;
    uint8_t  window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t bank_switch_func; // protected mode function to do bank swaps
    uint16_t pitch; // bytes in one scanline
    uint16_t width;
    uint16_t height;
    uint8_t  char_cell_width;
    uint8_t  char_cell_height;
    uint8_t  planes;
    uint8_t  bpp;
    uint8_t  banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  image_pages;
    uint8_t  __resv0;

    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t reserved_mask;
    uint8_t reserved_position;
    uint8_t direct_color_attributes;

    // in the case of a full (non-banked) linear framebuffer
    uint32_t framebuffer_paddr;
    uint32_t framebuffer_paddr_screen_end;
    uint16_t framebuffer_off_screen_size;
    uint8_t  __resv1[206];
} __attribute__((packed));

#include "gfx.h"
extern struct gfx_funcs vbe_funcs;

// call only once during setup
void vbe_gather_info();
unsigned char vbe_set_mode(int xres, int yres); // returns 0 as failure and bpp as success


void vbe_clear();
void vbe_write_pixel_buffered(unsigned int x, unsigned int y, uint32_t color, char use_palette);
void vbe_copy_region_unbuffered(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int final_x, unsigned int final_y);
void vbe_fill_buffered(unsigned int start_x, unsigned int end_x, unsigned start_y, unsigned int end_y, uint32_t color, char use_palette);;
uint32_t vbe_read_pixel(unsigned int x, unsigned int y);
void vbe_hw_shift_pixels(unsigned int pixels);
void vbe_hw_shift_scanlines(unsigned int scanlines);
#endif
