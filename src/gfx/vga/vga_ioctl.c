#include "lowlevel.h"
#include <kernel.h>
#include <kernel_spinlock.h>
#include <mm/kernel_memory.h>
#include "gfx/vga.h"
#include "gfx/vga/vga_funcs.h"
#include "gfx.h"
#include <bits/ioctl/fb_ioctl.h>
#include <string.h>
#include <errno.h>

enum vga_display_modes {
    VGA_13,
    VGA_X,
    VGA_X_WIDE,
    VGA_12,
    VGA_12_WIDE
};

#define VGA_RGB8 \
.red_mask = 3,\
.red_position = 5, \
.green_mask = 3, \
.green_position = 2, \
.blue_mask = 2, \
.blue_position = 0,

static const struct fb_info vga_modes[] = {
    [VGA_13]      = (struct fb_info) {
        .id = VGA_13,
        .type = FBIT_LINEAR,
        .pitch = 320,
        .width = 320,
        .height = 200,
        .planes = 1,
        .bpp = 8,

        VGA_RGB8

    },
    [VGA_X]       = (struct fb_info) {
        .id = VGA_X,
        .type = FBIT_UNCHAINED,
        .pitch = 80,
        .width = 320,
        .height = 240,
        .planes = 4,
        .bpp = 8,

        VGA_RGB8
    },
    [VGA_X_WIDE]  = (struct fb_info) {
        .id = VGA_X_WIDE,
        .type = FBIT_UNCHAINED,
        .pitch = 90,
        .width = 360,
        .height = 240,
        .planes = 4,
        .bpp = 8,

        VGA_RGB8
    },
    [VGA_12]      = (struct fb_info) {
        .id = VGA_12,
        .type = FBIT_PLANAR,
        .pitch = 80,
        .width = 640,
        .height = 480,
        .planes = 4,
        .bpp = 4,
    },
    [VGA_12_WIDE] = (struct fb_info) {
        .id = VGA_12_WIDE,
        .type = FBIT_PLANAR,
        .pitch = 90,
        .width = 720,
        .height = 480,
        .planes = 4,
        .bpp = 4,
    },
};


void vga_get_modeline(struct vesa_modeline * mode) {
    uint8_t vga_misc = inb(VGA_MISC_OUT_REG_RD);
    mode->hsync_polarity = !(vga_misc & (1<<7));
    mode->vsync_polarity = !(vga_misc & (1<<6));
    mode->clock_khz = (vga_misc & 0b1100) >> 2;

    uint8_t overflows = vga_rreg(VGA_CRTC_DATA_REG, 7);

    mode->vertical        = vga_rreg(VGA_CRTC_DATA_REG, 0x12) |
            (overflows & 0b10 ? 0x100 : 0) |
            (overflows & 0x40 ? 0x200 : 0);
    mode->vertical += 1;

    unsigned short start_vertical_retrace = vga_rreg(VGA_CRTC_DATA_REG, 0x10);
    start_vertical_retrace |= overflows & 0b100 ? 0x100 : 0;
    start_vertical_retrace |= overflows & 0x80 ? 0x200 : 0;

    mode->vertical_fporch = start_vertical_retrace;
    mode->vertical_fporch -= mode->vertical;
    mode->vertical_fporch -= mode->overscan;

    mode->vertical_sync  = vga_rreg(VGA_CRTC_DATA_REG, 0x11) - start_vertical_retrace;
    mode->vertical_sync &= 0x0F;

    mode->overscan = vga_rreg(VGA_CRTC_DATA_REG, 0x15) - mode->vertical + 1;

    unsigned short total_vertical = vga_rreg(VGA_CRTC_DATA_REG, 0x6);
    total_vertical |= overflows & 1 ? 0x100 : 0;
    total_vertical |= overflows & 0x20 ? 0x200 : 0;
    total_vertical ++;

    mode->vertical_bporch = total_vertical - start_vertical_retrace - mode->vertical_sync - mode->overscan;

    mode->horizontal = vga_rreg(VGA_CRTC_DATA_REG, 0x2);
    mode->horizontal *= VGA_DOT_DIV;

    mode->horizontal_fporch = vga_rreg(VGA_CRTC_DATA_REG, 0x4);
    mode->horizontal_fporch *= VGA_DOT_DIV;
    mode->horizontal_fporch -= mode->horizontal + 2 * mode->overscan;


    mode->horizontal_sync = vga_rreg(VGA_CRTC_DATA_REG, 0x5) & 0x1F;
    mode->horizontal_sync -= (mode->horizontal + mode->horizontal_fporch + 2 * mode->overscan) / VGA_DOT_DIV;
    mode->horizontal_sync &= 0x1F;
    mode->horizontal_sync *= VGA_DOT_DIV;

    mode->horizontal_bporch = vga_rreg(VGA_CRTC_DATA_REG, 0) + 5;
    mode->horizontal_bporch *= VGA_DOT_DIV;
    mode->horizontal_bporch -= 2*mode->overscan + mode->horizontal + mode->horizontal_fporch + mode->horizontal_sync;

    // final corrections
    mode->horizontal_bporch += mode->overscan * 2;
    mode->vertical_fporch += mode->overscan;
    mode->vertical_bporch += mode->overscan;
}

long vga_ioctl(unsigned long cmd, void * arg) {
    unsigned long n = (uintptr_t)arg;
    struct vga_misc_params * params = arg;

    switch (cmd) {
        case FB_GET_ACTIVE_MODE:
            if (!paging_check_address_range(arg, sizeof(struct fb_info), 1, 0))
                return -EFAULT;
            spinlock_acquire(&gfx_spinlock);
            if (display_width == 320) {
                if (display_height == 200) {
                    memcpy(arg, &vga_modes[VGA_13], sizeof(struct fb_info));
                } else {
                    memcpy(arg, &vga_modes[VGA_X], sizeof(struct fb_info));
                }
            } else if (display_width == 360)
                memcpy(arg, &vga_modes[VGA_X_WIDE], sizeof(struct fb_info));
            else if (display_width == 640)
                memcpy(arg, &vga_modes[VGA_12], sizeof(struct fb_info));
            else if (display_width == 720)
                memcpy(arg, &vga_modes[VGA_12_WIDE], sizeof(struct fb_info));
            else {
                spinlock_release(&gfx_spinlock);
                return -EIO;
            }
            spinlock_release(&gfx_spinlock);
            return 0;
        case FB_SWITCH_PLANE:
            spinlock_acquire(&gfx_spinlock);
            vga_wreg(VGA_SEQ_DATA_REG, 2, n);
            spinlock_release(&gfx_spinlock);
            return 0;
        case FB_GET_MODES:
            if (arg == NULL)
                return VGA_12_WIDE + 1;
            if (!paging_check_address_range(arg, sizeof(vga_modes), 1, 0))
                return -EFAULT;
            memcpy(arg, vga_modes, sizeof(vga_modes));
            return 0;
        case FB_SET_MODE:
            switch ((uintptr_t)arg) {
                case VGA_13:
                    vga_set_mode_13();
                    break;
                case VGA_X:
                    vga_set_mode_X();
                    break;
                case VGA_X_WIDE:
                    vga_set_mode_X_wide();
                    break;
                case VGA_12:
                    vga_set_mode_12();
                    break;
                case VGA_12_WIDE:
                    vga_set_mode_12_wide();
                    break;
                default:
                    break;
            }
            return 0;
        case FB_GET_MODELINE:
            if (arg == NULL)
                return -EFAULT;
            if (!paging_check_address_range(arg, sizeof(struct vesa_modeline), 1, 0))
                return -EFAULT;
            spinlock_acquire(&gfx_spinlock);
            vga_get_modeline(arg);
            spinlock_release(&gfx_spinlock);
            return 0;
        case FB_SET_MODELINE:
            if (arg == NULL)
                return -EFAULT;
            if (!paging_check_address_range(arg, sizeof(struct vesa_modeline), 0, 0))
                return -EFAULT;
            spinlock_acquire(&gfx_spinlock);
            vga_load_timings(*(struct vesa_modeline*)arg, ((struct vesa_modeline*)arg)->horizontal);
            spinlock_release(&gfx_spinlock);
            return 0;
        case FB_SET_HW_SCROLL:
            spinlock_acquire(&gfx_spinlock);
            vga_wreg(VGA_CRTC_DATA_REG, 0xD, (uintptr_t)arg >> 0);
            vga_wreg(VGA_CRTC_DATA_REG, 0xC, (uintptr_t)arg >> 8);
            spinlock_release(&gfx_spinlock);
            return 0;
        case FB_GET_VGA_MISC:
            if (arg == NULL)
                return -EFAULT;
            if (!paging_check_address_range(arg, sizeof(struct vga_misc_params), 1, 0))
                return -EFAULT;
            spinlock_acquire(&gfx_spinlock);

            params->hicolor = vga_rreg(VGA_GC_DATA_REG, 5) & VGA_GC_5_SHIFT256 ? 1 : 0;
            params->scan_doubling = vga_scan_doubling;
            params->clock_halving = vga_clock_halving;
            params->data_per_scanline = vga_rreg(VGA_CRTC_DATA_REG, 0x13);
            params->pixels_per_address = vga_pixels_per_address;
            params->addressing_mode = vga_addressing_mode;
            params->access_mode = current_vga_mode;

            params->actual_width = display_width;
            params->actual_height = display_height;

            spinlock_release(&gfx_spinlock);
            return 0;
        case FB_SET_VGA_MISC:
            if (arg == NULL)
                return -EFAULT;
            if (!paging_check_address_range(arg, sizeof(struct vga_misc_params), 0, 0))
                return -EFAULT;
            spinlock_acquire(&gfx_spinlock);

            uint8_t gc_mode = vga_rreg(VGA_GC_DATA_REG, 5) & ~VGA_GC_5_SHIFT256;
            vga_wreg(VGA_GC_DATA_REG, 5, gc_mode | (params->hicolor ? VGA_GC_5_SHIFT256 : 0));

            uint8_t mslr = vga_rreg(VGA_CRTC_DATA_REG, 0x9) & ~0x80;
            vga_wreg(VGA_CRTC_DATA_REG, 0x9, mslr | (params->scan_doubling ? 0x80 : 0));
            vga_scan_doubling = 1;

            uint8_t crtc_mode_control = vga_rreg(VGA_CRTC_DATA_REG, VGA_CRTC_MODE_CONTROL) & ~0b100;
            vga_wreg(VGA_CRTC_DATA_REG, VGA_CRTC_MODE_CONTROL, crtc_mode_control | (params->clock_halving ? 0b100 : 0));
            vga_clock_halving = params->clock_halving;

            vga_wreg(VGA_CRTC_DATA_REG, 0x13, params->data_per_scanline);

            vga_pixels_per_address = params->pixels_per_address;

            vga_set_addressing_mode(params->addressing_mode);

            current_vga_mode = params->access_mode;

            int new_width = params->actual_width;
            int new_height = params->actual_height;
            if (new_width * new_height > SHADOW_FRAMEBUFFER_SIZE) {
                if (new_width > SHADOW_FRAMEBUFFER_SIZE) { // what?
                    display_width = 720;
                    display_height = 480;
                }
                display_width = new_width;
                display_height = SHADOW_FRAMEBUFFER_SIZE / new_width;
            } else {
                display_width = new_width;
                display_height = new_height;
            }

            spinlock_release(&gfx_spinlock);
            return 0;
        default:
            return -ENOTTY;
    }
}