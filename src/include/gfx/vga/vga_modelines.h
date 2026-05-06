#ifndef VGA_MODESETTINGS_H
#define VGA_MODESETTINGS_H

enum vga_clocks {
    DOT8,           // 25MHz
    DOT9,           // 28MHz
    PARADISE_42, // 42MHz, nonstandard!
    UNSPEC
};

// set porches as if no overscan
// TODO: decouple structure from VGA
struct vesa_modeline {
    int vertical, horizontal;
    int vertical_fporch, horizontal_fporch;
    int vertical_bporch, horizontal_bporch;
    int vertical_sync, horizontal_sync;

    char vsync_polarity, hsync_polarity;

    char overscan;

    char clock_type; // used in VGA
};

// see the notes in vga.c for these
extern char vga_pixels_per_address;
extern char vga_scan_doubling;

void vga_load_timings(struct vesa_modeline timings, int actual_width);

extern const struct vesa_modeline vga_720x480;
extern const struct vesa_modeline vga_640x400;
extern const struct vesa_modeline vga_640x480;

#endif