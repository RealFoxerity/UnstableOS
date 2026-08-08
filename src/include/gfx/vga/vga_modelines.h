#ifndef VGA_MODESETTINGS_H
#define VGA_MODESETTINGS_H

enum vga_clocks {
    DOT8,           // 25MHz
    DOT9,           // 28MHz
    PARADISE_42, // 42MHz, nonstandard!
    UNSPEC
};

#include <bits/ioctl/fb_ioctl.h>

// see the notes in vga.c for these
extern unsigned char vga_pixels_per_address;
extern char vga_scan_doubling;
extern char vga_clock_halving;
extern enum vga_addressing_modes vga_addressing_mode;
void vga_load_timings(struct vesa_modeline timings, int actual_width);

extern const struct vesa_modeline vga_720x480;
extern const struct vesa_modeline vga_640x400;
extern const struct vesa_modeline vga_640x480;

#endif