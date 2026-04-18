#include "gfx.h"
#include "kernel_spinlock.h"
#include <stddef.h>

unsigned int console_font_width = 8;
unsigned int console_font_height = 8;

const struct gfx_funcs * current_video_funcs = NULL;

static const unsigned char * console_font = NULL;
static unsigned int console_font_chars = 0;

#define COMPONENT_TO_RGB32(r, g, b) (((r) << 24) | ((g) << 16) | ((b) << 8))

spinlock_t gfx_spinlock = {0}; // to not race on I/O activity

// the default xterm colors
const uint32_t console_colors[16] = {
    COMPONENT_TO_RGB32(0, 0, 0),
    COMPONENT_TO_RGB32(128, 0, 0),
    COMPONENT_TO_RGB32(0, 128, 0),
    COMPONENT_TO_RGB32(128, 128, 0),
    COMPONENT_TO_RGB32(0, 0, 128),
    COMPONENT_TO_RGB32(128, 0, 128),
    COMPONENT_TO_RGB32(0, 128, 128),
    COMPONENT_TO_RGB32(192, 192, 192),
    COMPONENT_TO_RGB32(128, 128, 128),
    COMPONENT_TO_RGB32(255, 0, 0),
    COMPONENT_TO_RGB32(0, 255, 0),
    COMPONENT_TO_RGB32(255, 255, 0),
    COMPONENT_TO_RGB32(0, 0, 255),
    COMPONENT_TO_RGB32(255, 0, 255),
    COMPONENT_TO_RGB32(0, 255, 255),
    COMPONENT_TO_RGB32(255, 255, 255)
};

void gfx_swap_buffers() {
    current_video_funcs->swap_region(0, display_width - 1, 0, display_height - 1);
}

void load_font(
    const unsigned char * console_font_bitmap, unsigned int chars,
    unsigned int char_width, unsigned int char_height
) {
    if (char_width == 0 || char_height == 0) return;
    console_font_width = char_width;
    console_font_height = char_height;

    console_font = console_font_bitmap;
    console_font_chars = chars;
}

void gfx_blit_char_buffered(
    unsigned int c,
    unsigned int x, unsigned int y,
    uint32_t fg_color, uint32_t bg_color,
    char use_palette,
    unsigned int size_mult
) {
    if (size_mult == 0) return;
    if (c >= console_font_chars) return;
    if (x >= display_width || y >= display_height) return;

    for (unsigned int fy = 0; fy < console_font_height * size_mult; fy++) {
        if (fy + y >= display_height) break;
        for (unsigned int fx = 0; fx < console_font_width * size_mult; fx++) {
            if (fx + x >= display_width) break;
            if (
                console_font[c * console_font_height + fy/size_mult] &
                (1 << (7 - fx/size_mult))
            )
                current_video_funcs->write_pixel_buffered(fx + x, fy + y, fg_color, use_palette);
            else
                current_video_funcs->write_pixel_buffered(fx + x, fy + y, bg_color, use_palette);
        }
    }
}

void gfx_blit_char(
    unsigned int c,
    unsigned int x, unsigned int y,
    uint32_t fg_color, uint32_t bg_color,
    char use_palette,
    unsigned int size_mult
) {
    gfx_blit_char_buffered(c, x, y, fg_color, bg_color, use_palette, size_mult);
    current_video_funcs->swap_region(x, x + size_mult * console_font_width, y, y + size_mult * console_font_height);
}

void gfx_hw_scroll_scanlines(unsigned int scanline) {
    if (current_video_funcs->hw_shift_scanlines)
        current_video_funcs->hw_shift_scanlines(scanline);
    else
        current_video_funcs->hw_shift_pixels(scanline * display_width);
}