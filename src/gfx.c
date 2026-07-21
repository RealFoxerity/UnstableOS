#include "gfx.h"
#include "kernel_spinlock.h"
#include <stddef.h>

#include "kernel.h"
#include "string.h"
#include "gfx/vga/vga_funcs.h"

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

#include "mm/kernel_memory.h"
// part of early init
// not thread safe, so disables interrupts
spinlock_t framebuffer_lock = {0};
uint32_t * back_framebuffer = NULL;
size_t back_framebuffer_w = 0;
size_t back_framebuffer_h = 0;

void gfx_unmap_back_framebuffer() {
    spinlock_acquire(&framebuffer_lock);

    back_framebuffer_w = 0;
    back_framebuffer_h = 0;
    kfree(back_framebuffer);
    back_framebuffer = NULL;

    spinlock_release(&framebuffer_lock);
}

void * gfx_realloc_back_framebuffer(size_t width, size_t height) {
    spinlock_acquire(&framebuffer_lock); // needs to disable interrupts
    kfree(back_framebuffer);

    if (width * height * sizeof(uint32_t) <= BACK_FRAMEBUFFER_MAX_SIZE &&
        kalloc_get_free_memory() > width * height * sizeof(uint32_t) * 2 &&
        pf_get_free_memory() > width * height * sizeof(uint32_t) * 10
    ) {
        back_framebuffer = kalloc(width * height * sizeof(uint32_t));
        back_framebuffer_w = width;
        back_framebuffer_h = height;

        if (back_framebuffer == NULL) {
            alloc_failed:
            spinlock_release(&framebuffer_lock);

            gfx_unmap_back_framebuffer();
            kprintf("Warning: cannot allocate a back framebuffer!\n");

            /*
            vga_init_graphics();
            panic("Failed to allocate memory for a back framebuffer!\n");
            */

            return NULL;
        }
        memset(back_framebuffer, 0, width * height * sizeof(uint32_t));
    } else goto alloc_failed;
    spinlock_release(&framebuffer_lock);

    return back_framebuffer;
}

size_t framebuffer_size = 0;
void * gfx_remap_framebuffer(void * phys_start, size_t fb_size, unsigned int flags) {
    if (phys_start == NULL) {
        kprintf("Warning: specified NULL framebuffer address, ignoring request\n");
        return NULL;
    }

    if (fb_size &   (PAGE_SIZE_NO_PAE - 1)) {
        fb_size &= ~(PAGE_SIZE_NO_PAE - 1);
        fb_size +=   PAGE_SIZE_NO_PAE;
    }
    if (fb_size > LINEAR_FRAMEBUFFER_MAX_SIZE) {
        kprintf("Warning: tried to map a framebuffer too large, mapping only the first %d MiB\n",
            LINEAR_FRAMEBUFFER_MAX_SIZE / 1024 / 1024);
        fb_size = LINEAR_FRAMEBUFFER_MAX_SIZE;
    }

    spinlock_acquire(&framebuffer_lock); // needs to disable interrupts

    framebuffer_size = fb_size;

    // first unmap everything so that we don't try to doublemap later
    paging_unmap(LINEAR_FRAMEBUFFER_START, LINEAR_FRAMEBUFFER_MAX_SIZE);
    for (size_t chunk = 0; chunk < fb_size / PAGE_SIZE_NO_PAE; chunk++) {
        paging_map_phys_addr(phys_start + chunk*PAGE_SIZE_NO_PAE,
            LINEAR_FRAMEBUFFER_START + chunk*PAGE_SIZE_NO_PAE,
            PTE_PDE_PAGE_WRITABLE | flags);
    }
    spinlock_release(&framebuffer_lock);

    return LINEAR_FRAMEBUFFER_START;
}