#ifndef GFX_H
#define GFX_H
#include <stdint.h>
#include <unistd.h>

#define LINEAR_FRAMEBUFFER_START ((void*)0xD0000000)
#define LINEAR_FRAMEBUFFER_MAX_SIZE      0x10000000
extern size_t framebuffer_size;

extern unsigned int display_width, display_height;

extern unsigned int console_font_width;
extern unsigned int console_font_height;

#define display_width_chars (display_width/console_font_width)
#define display_height_chars (display_height/console_font_height)

enum console_colors_palette {
    CONSOLE_COLOR_BLACK,
    CONSOLE_COLOR_RED,
    CONSOLE_COLOR_GREEN,
    CONSOLE_COLOR_YELLOW,
    CONSOLE_COLOR_BLUE,
    CONSOLE_COLOR_MAGENTA,
    CONSOLE_COLOR_CYAN,
    CONSOLE_COLOR_WHITE,
    CONSOLE_COLOR_BRIGHT_BLACK,
    CONSOLE_COLOR_BRIGHT_RED,
    CONSOLE_COLOR_BRIGHT_GREEN,
    CONSOLE_COLOR_BRIGHT_YELLOW,
    CONSOLE_COLOR_BRIGHT_BLUE,
    CONSOLE_COLOR_BRIGHT_MAGENTA,
    CONSOLE_COLOR_BRIGHT_CYAN,
    CONSOLE_COLOR_BRIGHT_WHITE,
};

// this structure only enforces moving to be unbuffered
// don't make assumptions of buffered operations to be actually buffered
// assuming hw_shift_scanlines is null, shift_pixels is used
// always set at least one to something

struct gfx_funcs {
    void (*clear_screen)(); // direct, unbuffered, use write pixel otherwise
    void (*swap_region)(unsigned int start_x, unsigned int end_x, unsigned int start_y, unsigned int end_y);

    void (*write_framebuffer_buffered)(unsigned int x, unsigned int y, uint32_t raw);
    uint32_t (*read_framebuffer)(unsigned int x, unsigned int y);

    void (*write_pixel_buffered)(unsigned int x, unsigned int y, uint32_t color, char use_palette);

    void (*fill_buffered)(unsigned int start_x, unsigned int end_x, unsigned start_y, unsigned int end_y, uint32_t color, char use_palette);
    void (*copy_region_unbuffered)(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int final_x, unsigned int final_y);
    void (*hw_shift_pixels)(unsigned int pixels);
    void (*hw_shift_scanlines)(unsigned int lines);
};

extern const struct gfx_funcs * current_video_funcs;
extern const uint32_t console_colors[16];

void gfx_swap_buffers();
void gfx_hw_scroll_scanlines(unsigned int scanline);

void load_font(
    const unsigned char * console_font_bitmap, unsigned int chars,
    unsigned int char_width, unsigned int char_height
);

void gfx_blit_char_buffered(
    unsigned int c,
    unsigned int x, unsigned int y,
    uint32_t fg_color, uint32_t bg_color,
    char use_palette,
    unsigned int size_mult
);

void gfx_blit_char(
    unsigned int c,
    unsigned int x, unsigned int y,
    uint32_t fg_color, uint32_t bg_color,
    char use_palette,
    unsigned int size_mult
);

void * gfx_realloc_back_framebuffer(size_t width, size_t height);
void * gfx_remap_framebuffer(void * phys_start, size_t fb_size, unsigned int flags);
void gfx_unmap_back_framebuffer();

// heap allocated double buffered framebuffer for drivers to utilize when writing/reading pixels
// note: by itself, not used; changed by gfx_remap_framebuffer; always check if null! (not enough memory to allocate)
#define BACK_FRAMEBUFFER_MAX_SIZE (16*1024*1024) // so that we don't waste 100MB+ of heap on double buffering
extern uint32_t * back_framebuffer;
extern size_t back_framebuffer_w;
extern size_t back_framebuffer_h;

#include "kernel_spinlock.h"
extern spinlock_t framebuffer_lock; // use when accessing the back_framebuffer

extern spinlock_t gfx_spinlock;
#endif