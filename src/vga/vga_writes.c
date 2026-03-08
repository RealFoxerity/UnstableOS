#include "../include/vga.h"
#include "../include/kernel_spinlock.h"
#include "../../libc/src/include/string.h"

#define SHADOW_FRAMEBUFFER_SIZE (800*600) // largest one we support

// empty areas of memory we wouldn't be managing
//unsigned char * shadow_framebuffer = (unsigned char *)0x00007E00; // turns out this is where grub stores multiboot structures
unsigned char * vga_font8x16 = (unsigned char *)0x00005000;

// note: this is 480KB!
// normal compilers place this into uninitialized memory
// thus this buffer is 0 bytes inside the elf file
unsigned char shadow_framebuffer[SHADOW_FRAMEBUFFER_SIZE];

unsigned int vga_pixel_offset = 0;


spinlock_t vga_spinlock = {0}; // to not race on I/O activity

static inline void vga_set_plane(int plane) {
    static int last_plane = 0;
    plane &= 3;
    if (last_plane != plane) // io operations are costly
        vga_wreg(VGA_SEQ_DATA_REG, 2, 1<<plane);
    last_plane = plane;
}


int vga_read_pixel(unsigned int x, unsigned int y) {
    if (x >= display_width || y >= display_height) return 0;
    return shadow_framebuffer[y * display_width + x];
}

struct mode12_planes {
    union {
        struct {
            unsigned char p0, p1, p2, p3;
        };
        unsigned char planes[4];
    };
};
static struct mode12_planes mode12_construct_pixel_from_shadow(int x, int y) {
    struct mode12_planes planes = {0};
    x -= x%8;
    for (int i = 0; i < 8; i++) {
        if (shadow_framebuffer[y*display_width + x + i] & 0b0001)
            planes.p0 |= 1 << (7 - i); 
        if (shadow_framebuffer[y*display_width + x + i] & 0b0010)
            planes.p1 |= 1 << (7 - i); 
        if (shadow_framebuffer[y*display_width + x + i] & 0b0100)
            planes.p2 |= 1 << (7 - i); 
        if (shadow_framebuffer[y*display_width + x + i] & 0b1000)
            planes.p3 |= 1 << (7 - i); 
    }
    return planes;
} 

void vga_write_pixel_buffered(unsigned int x, unsigned int y, unsigned char color, char use_palette) {
    if (x >= display_width || y >= display_height) return;

    if (use_palette && current_vga_mode != MODE12)
        color = console_colors[color & 0xF];

    shadow_framebuffer[y * display_width + x] = color;
}

// /4 for planes
#define VGA_UNCHAINED_GET_PAGE_INDEX(x, y) ((y)*(display_width/4) + (x)/4)
void vga_write_pixel(unsigned int x, unsigned int y, unsigned char color, char use_palette) {
    if (x >= display_width || y >= display_height) return;
    
    if (use_palette && current_vga_mode != MODE12)
        color = console_colors[color & 0xF];

    //for (int i = 0; i < 10000000; i++);
    vga_write_pixel_buffered(x, y, color, 0);
    vga_swap_region(x, x, y, y);
}

unsigned int console_font_width = 8;
unsigned int console_font_height = 8;
static const unsigned char * console_font = NULL;
static unsigned int console_font_chars = 0;
void vga_load_font(
    const unsigned char * console_font_bitmap, unsigned int chars,
    unsigned int char_width, unsigned int char_height
) {
    if (char_width == 0 || char_height == 0) return;
    console_font_width = char_width;
    console_font_height = char_height;

    console_font = console_font_bitmap;
    console_font_chars = chars;
}

void vga_blit_char_buffered(
    unsigned int c,
    unsigned int x, unsigned int y,
    unsigned char fg_color, unsigned char bg_color,
	char use_palette,
    unsigned int size_mult
) {
    if (size_mult == 0) return;
    if (c >= console_font_chars) return;
    if (x >= display_width || y >= display_height) return;

    if (use_palette && current_vga_mode != MODE12) {
        fg_color = console_colors[fg_color & 0xF];
        bg_color = console_colors[bg_color & 0xF];
    }

    // mode 12 already uses palette by default
    for (unsigned int fy = 0; fy < console_font_height * size_mult; fy++) {
        if (fy >= display_height) break;
        for (unsigned int fx = 0; fx < console_font_width * size_mult; fx++) {
            if (fx >= display_width) break;
            if (
                console_font[c * console_font_height + fy/size_mult] &
                (1 << (7 - fx/size_mult))
            )
                shadow_framebuffer[(fy + y) * display_width + fx + x] = fg_color;
            else
                shadow_framebuffer[(fy + y) * display_width + fx + x] = bg_color;
        }
    }
}

void vga_blit_char(
    unsigned int c,
    unsigned int x, unsigned int y,
    unsigned char fg_color, unsigned char bg_color,
    char use_palette,
    unsigned int size_mult
) {
    if (size_mult == 0) return;
    if (c >= console_font_chars) return;
    if (x >= display_width || y >= display_height) return;

    if (use_palette && current_vga_mode != MODE12) {
        fg_color = console_colors[fg_color & 0xF];
        bg_color = console_colors[bg_color & 0xF];
    }

    // keep the shadow framebuffer consistent
    // since we change the palette colors, setting 1 would change them yet again
    vga_blit_char_buffered(c, x, y, fg_color, bg_color, 0, size_mult);
    vga_swap_region(x, x+console_font_width, y, y+console_font_height);
}

void vga_clear_screen_buffered() {
    memset(shadow_framebuffer, 0, SHADOW_FRAMEBUFFER_SIZE);
}

void vga_clear_screen() {
    spinlock_acquire_interruptible(&vga_spinlock);
    vga_clear_screen_buffered();
    // select all planes to speed up clear
    vga_wreg(VGA_SEQ_DATA_REG, 2, 0xF);
    
    switch (current_vga_mode) {
        case CHAINED:
            memset(VGA_PAGE_ADDR + vga_pixel_offset, 0, display_width*display_height);
            break;
        case UNCHAINED:
            // 4 planes
            memset(VGA_PAGE_ADDR + vga_pixel_offset/4, 0, display_width*display_height/4);
            break;
        case MODE12:
            // 8 pixels per byte
            memset(VGA_PAGE_ADDR + vga_pixel_offset/8, 0, display_width*display_height/8);
            break;
        default:
            break;
    }
    spinlock_release(&vga_spinlock);
}

void vga_swap_region(unsigned int start_x, unsigned int end_x, unsigned int start_y, unsigned int end_y) {
    if (start_x >= display_width) start_x = display_width - 1;
    if (start_y >= display_height) start_y = display_height - 1;
    if (end_x >= display_width) end_x = display_width - 1;
    if (end_y >= display_height) end_y = display_height - 1;

    if (end_x < start_x) {
        unsigned int temp = end_x;
        end_x = start_x;
        start_x = temp;
    }

    if (end_y < start_y) {
        unsigned int temp = end_y;
        end_y = start_y;
        start_y = temp;
    }

    struct mode12_planes planes;

    spinlock_acquire_interruptible(&vga_spinlock);

    switch (current_vga_mode) {
        case CHAINED:
            for (int y = start_y; y <= end_y; y++)
                memcpy(VGA_PAGE_ADDR + ((y*display_width + start_x + vga_pixel_offset) & 0xFFFF), shadow_framebuffer + y*display_width + start_x, end_x - start_x + 1);
            break;
        case UNCHAINED:
            for (int i = 0; i < 4; i++) {
                vga_set_plane(i);
                for (unsigned int x = start_x + i; x <= end_x; x+=4) {
                    for (unsigned int y = start_y; y <= end_y; y++) {
                        VGA_PAGE_ADDR[
                            (VGA_UNCHAINED_GET_PAGE_INDEX(x, y) + vga_pixel_offset/4) & 0xFFFF] =
                                shadow_framebuffer[y * display_width + x];
                    }
                }
            }
            break;
        case MODE12:
            for (int i = 0; i < 4; i++) {
                vga_set_plane(i);
                for (unsigned int x = start_x; x <= end_x; x+=8) {
                    for (unsigned int y = start_y; y <= end_y; y++) {
                        planes = mode12_construct_pixel_from_shadow(x, y);
                        VGA_PAGE_ADDR[
                            ((y*display_width + x + vga_pixel_offset)/8) & 0xFFFF] =
                                planes.planes[i];
                    }
                }
            }
            break;
    }
    spinlock_release(&vga_spinlock);
}

void vga_swap_buffers() {
    vga_swap_region(0, display_width-1, 0, display_height-1);
}

void vga_fill_buffered(unsigned int start_x, unsigned int end_x, unsigned start_y, unsigned int end_y, unsigned char color, char use_palette) {
    if (start_x >= display_width) start_x = display_width - 1;
    if (start_y >= display_height) start_y = display_height - 1;
    if (end_x >= display_width) end_x = display_width - 1;
    if (end_y >= display_height) end_y = display_height - 1;

    if (use_palette && current_vga_mode != MODE12)
        color = console_colors[color & 0xF];

    for (unsigned int x = start_x; x <= end_x; x++) {
        for (unsigned int y = start_y; y <= end_y; y++) {
            shadow_framebuffer[y*display_width + x] = color;
        }
    }
}

void vga_fill(unsigned int start_x, unsigned int end_x, unsigned start_y, unsigned int end_y, unsigned char color, char use_palette) {
    if (start_x >= display_width) start_x = display_width - 1;
    if (start_y >= display_height) start_y = display_height - 1;
    if (end_x >= display_width) end_x = display_width - 1;
    if (end_y >= display_height) end_y = display_height - 1;

    if (use_palette && current_vga_mode != MODE12)
        color = console_colors[color & 0xF];
    vga_fill_buffered(start_x, end_x, start_y, end_y, color, 0);
    vga_swap_region(start_x, end_x, start_y, end_y);
}

void vga_hw_shift_pixels(unsigned int pixels) {
    if (pixels > display_width*display_height) {
        memset(shadow_framebuffer, 0, display_width*display_height);
    } else {
        memmove(shadow_framebuffer, shadow_framebuffer + pixels, display_width*display_height - pixels);
        memset(shadow_framebuffer + display_width*display_height - pixels, 0, pixels);
    }

    // due to the way 
    if (current_vga_mode == CHAINED) {
        vga_swap_buffers();
        return;
    }

    #ifndef VGA_NO_HW_SHIFT
    spinlock_acquire_interruptible(&vga_spinlock);
    vga_pixel_offset += pixels;

    switch (current_vga_mode) {
        case MODE12:
            vga_wreg(VGA_CRTC_DATA_REG, 0xD, (vga_pixel_offset/8));
            vga_wreg(VGA_CRTC_DATA_REG, 0xC, (vga_pixel_offset/8) >> 8);
            break;
        case UNCHAINED:
        default:
            vga_wreg(VGA_CRTC_DATA_REG, 0xD, (vga_pixel_offset)/4);
            vga_wreg(VGA_CRTC_DATA_REG, 0xC, (vga_pixel_offset)/4 >> 8);
            break;
    }
    spinlock_release(&vga_spinlock);
    #else
    vga_swap_buffers();
    #endif
}

void vga_hw_scroll_scanlines(unsigned int scanlines) {
    vga_hw_shift_pixels(scanlines * display_width);
}


void vga_move_region(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int final_x, unsigned int final_y) {
    if (x >= display_width || y >= display_height) return;

    if (x + width > display_width)
        width = display_width - x;
    if (y + height > display_height)
        height = display_height - y;

    if (final_x >= display_width || final_y >= display_height) {
        for (int i = 0; i < display_height; i++) {
            memset(shadow_framebuffer + (i * display_width) + x, 0, width);
        }
        vga_swap_region(x, x+width - 1, y, y+height - 1);
        return;
    }

    unsigned int final_width = width, final_height = height;

    if (final_x + width > display_width)
        final_width = display_width - final_x;
    if (final_y + height > display_height)
        final_height = display_height - final_y;

    if (final_y > y + height || final_y < y) {
        for (int i = 0; i < final_height; i++) {
            memcpy(shadow_framebuffer + (final_y+i)*display_width + final_x, shadow_framebuffer + (y+i)*display_width + x, final_width);
            memset(shadow_framebuffer + (y+i)*display_width + x, 0, width);
        }
    } else {
        for (int i = final_height - 1; i >= 0; i--) {
            memcpy(shadow_framebuffer + (final_y+i)*display_width + final_x, shadow_framebuffer + (y+i)*display_width + x, final_width);
            memset(shadow_framebuffer + (y+i)*display_width + x, 0, width);
        }
    }

    vga_swap_region(x, x+width - 1, y, y+height - 1);
    vga_swap_region(final_x, final_x+final_width - 1, final_y, final_y+final_height - 1);
}