#ifndef _BITS_IOCTL_FB_IOCTL_H
#define _BITS_IOCTL_FB_IOCTL_H

#ifndef __IOCTL_NO
#define __IOCTL_NO(major, func) ((((major) & 0x3F) << 10) | ((func) & 0x3FF))
#endif
#ifndef __IOCTL_DEV
#define __IOCTL_DEV(id) ((id) >> 10)
#endif

enum fb_info_types {
    FBIT_LINEAR,
    FBIT_PLANAR, // planes have colors
    FBIT_UNCHAINED // planes have pixels, e.g. odd/even planes
};
struct fb_info {
    unsigned short id;
    unsigned char type;

    unsigned long pitch; // bytes in one scanline
    unsigned long width;
    unsigned long height;
    unsigned short planes;
    unsigned char bpp;

    unsigned char red_mask; // if all masks zero, then it's a hard palette
    unsigned char red_position;
    unsigned char green_mask;
    unsigned char green_position;
    unsigned char blue_mask;
    unsigned char blue_position;
};


// set porches as if no overscan
// TODO: decouple structure from VGA
struct vesa_modeline {
    unsigned int vertical, horizontal;
    unsigned int vertical_fporch, horizontal_fporch;
    unsigned int vertical_bporch, horizontal_bporch;
    unsigned int vertical_sync, horizontal_sync;

    char vsync_polarity, hsync_polarity;

    unsigned char overscan;

    unsigned long clock_khz; // used in VGA
};

#include <UnstableOS/devs.h>

// ioctl(fd, FB_GET_ACTIVE_MODE, struct fb_info * info);
#define FB_GET_ACTIVE_MODE __IOCTL_NO(DEV_MAJ_FB, 0)
// ioctl(fd, FB_SWITCH_PLANE, unsigned long plane_bitmask);
#define FB_SWITCH_PLANE    __IOCTL_NO(DEV_MAJ_FB, 1)
// ioctl(fd, FB_GET_MODES, struct fb_info[] array);
// set array to NULL to get the count
#define FB_GET_MODES       __IOCTL_NO(DEV_MAJ_FB, 2)
// ioctl(fd, FB_SET_MODE, unsigned short id);
#define FB_SET_MODE        __IOCTL_NO(DEV_MAJ_FB, 3)
// ioctl(fd, FB_GET_MODELINE, struct vesa_modeline modeline);
#define FB_GET_MODELINE    __IOCTL_NO(DEV_MAJ_FB, 4)
// ioctl(fd, FB_SET_MODELINE, struct vesa_modeline * modeline);
#define FB_SET_MODELINE    __IOCTL_NO(DEV_MAJ_FB, 5)
// ioctl(fd, FB_SET_HW_SCROLL, unsigned long scroll_reg_value);
#define FB_SET_HW_SCROLL   __IOCTL_NO(DEV_MAJ_FB, 6)

//#define FB_GET_EDID        __IOCTL_NO(DEV_MAJ_FB, 7)

// there's currently no way to set a custom 256 color palette
// the default palette is RGB somewhat linearly mapped to 3:3:2
// the mode12 palette is the standard xterm 16 color one

enum vga_modes {
    CHAINED, // linear
    // planes hold pixels, so pixels 0-7 would be in planes
    // 0 1 2 3 0 1 2 3
    UNCHAINED,
    // planes hold color bits, each byte is 8 pixels
    MODE12,
};

enum vga_addressing_modes {
    // each of the 4 planes get used in the image creation
    VGA_AM_BYTE,
    // only 2 get used (almost never used)
    VGA_AM_WORD,
    // just 1 gets used, usually in chained modes
    VGA_AM_DWORD
};

struct vga_misc_params {
    unsigned char hicolor : 1; // 256 colors/8bpp
    // all scanlines will be displayed twice, halving the vertical resolution
    unsigned char scan_doubling : 1;
    // the entire clock will be halved (slowed down by 2), halving both resolutions
    unsigned char clock_halving : 1;
    // actual_width / (2 * vga_pixels_per_address)
    // used for setting correct resolution when loading a modeline
    // basically scan doubling but horizontally and more granular-ly
    unsigned char data_per_scanline;

    // used to calculate data_per_scanline automatically when loading modelines
    // not necessary to set when manually setting data_per_scanline like this
    // it's very confusing, but assume 4 for chain/unchained and 8 for planar
    unsigned char pixels_per_address;
    // likewise confusing, but basically how many planes it will skip when rendering
    enum vga_addressing_modes addressing_mode;
    // which mode should the kernel driver use to do the framebuffer console
    // if you don't care about seeing the console, set it to whatever :p
    enum vga_modes access_mode;

    // to correct the kernel's width and height
    // note that supplying values leading to higher pixel count than 640x480
    //  will silently get clamped to that via backbuffer_size/actual_width
    //  or failing that, just setting 640x480
    // this will ensure the kernel routines will still have a backbuffer for character blitting
    //  this is important especially in mode 12, where setting each pixel requires reading all 4 planes
    // having this value wrong, or clamped, will just make the kernel console not be the full size
    // this can't be a part of the modeline, since it's highly dependent on values from this structure
    unsigned short actual_width;
    unsigned short actual_height;
};
#define FB_GET_VGA_MISC    __IOCTL_NO(DEV_MAJ_FB, 0x100)
#define FB_SET_VGA_MISC    __IOCTL_NO(DEV_MAJ_FB, 0x101)
#endif