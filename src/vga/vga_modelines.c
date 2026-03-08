#include "../include/vga/vga_modelines.h"

/*
const struct vesa_modeline vga_1280x800 = {
    .clock_type = PARADISE_42,
    .horizontal = 1280,
    .horizontal_fporch = 48,
    .horizontal_sync = 32,
    .horizontal_bporch = 80,

    .vertical = 800,
    .vertical_fporch = 3,
    .vertical_sync = 10,
    .vertical_bporch = 6,

    .hsync_polarity = 1,
    .vsync_polarity = 0
};
*/

const struct vesa_modeline vga_640x400 = {
    .horizontal = 640,
    .horizontal_fporch = 16,
    .horizontal_sync = 96,
    .horizontal_bporch = 48,

    .vertical = 400,
    .vertical_fporch = 12,
    .vertical_sync = 2,
    .vertical_bporch = 35,

    .overscan = 8,

    .hsync_polarity = 0,
    .vsync_polarity = 1
};

const struct vesa_modeline vga_640x480 = {
    .horizontal = 640,
    .horizontal_fporch = 16,
    .horizontal_sync = 96,
    .horizontal_bporch = 48,

    .vertical = 480,
    .vertical_fporch = 10,
    .vertical_sync = 2,
    .vertical_bporch = 32,

    .overscan = 8,

    .hsync_polarity = 0,
    .vsync_polarity = 0
};

const struct vesa_modeline vga_720x480 = {
    .clock_type = DOT9,
    .horizontal = 720,
    // straight up just guessed these, not gonna even lie
    // but they work, so probably don't touch
    .horizontal_fporch = 23,
    .horizontal_sync = 112,
    .horizontal_bporch = 60,

    .vertical = 480,
    .vertical_fporch = 10,
    .vertical_sync = 2,
    .vertical_bporch = 32,

    .overscan = 8,

    .hsync_polarity = 0,
    .vsync_polarity = 0
};

/*
const struct vesa_modeline vga_800_600 = {
    .clock_type = PARADISE_42,
    .horizontal = 800,
    .horizontal_fporch = 40,
    .horizontal_sync = 128,
    .horizontal_bporch = 88,

    .vertical = 600,
    .vertical_fporch = 1,
    .vertical_sync = 4,
    .vertical_bporch = 22,

    .hsync_polarity = 1,
    .vsync_polarity = 1
};
*/

#define VGA_VERT_FPORCH_DEFAULT 10 // arbitrary value

// TODO: figure out how to read from EDID
#define VESA_M 600
#define VESA_C 40
#define VESA_K 128
#define VESA_J 20

#define VESA_C_PRIME (((VESA_C - VESA_J) * VESA_K)/256 + VESA_J)
#define VESA_M_PRIME ((VESA_K * VESA_M)/256)

// VESA specifies 8%, which is the same as /12.5
#define VESA_HOR_SYNC_DIV 12

#define VESA_VER_MIN_FPORCH 3
#define VESA_VER_MIN_BPORCH 6
#define VESA_VER_SYNC_BPORCH_MIN_USEC 550

#define VESA_VER_SYNC 2

// TODO: support interlace?
#define VESA_INTERLACE 0

#define VESA_CHARACTER_CELL_W 8

// VESA specifies clockstep of 0.25 MHz
#define VESA_CLOCKSTEP_KHZ 250
struct vesa_modeline vesa_modeline_from_refresh_rate(
    int width, int height,
    int overscan,
    int refresh_hz
) {
    width  -= width  % VESA_CHARACTER_CELL_W;
    height -= height % VESA_CHARACTER_CELL_W;

    int total_hor_active_pixels = width + 2 * overscan;

    int frame_time_usec = 1000000/refresh_hz;

    int scanline_khz = 
        (frame_time_usec - VESA_VER_SYNC_BPORCH_MIN_USEC) / 
        (height + (2 * overscan) + VESA_VER_MIN_FPORCH + VESA_INTERLACE);
    
    int vertical_sync_backporch = VESA_VER_SYNC_BPORCH_MIN_USEC/scanline_khz;
    if (vertical_sync_backporch < VESA_VER_SYNC + VESA_VER_MIN_BPORCH)
        vertical_sync_backporch = VESA_VER_SYNC + VESA_VER_MIN_BPORCH;

    int vertical_backporch = vertical_sync_backporch - VESA_VER_SYNC;
    int total_vert = height + 2 * overscan + vertical_sync_backporch + VESA_INTERLACE + VESA_VER_MIN_FPORCH;

    int ideal_clock_duty_cycle_percentage = VESA_C_PRIME - VESA_M_PRIME * scanline_khz / 1000;

    if (ideal_clock_duty_cycle_percentage < 20)
        ideal_clock_duty_cycle_percentage = 20;

    int horizontal_blank = total_hor_active_pixels * ideal_clock_duty_cycle_percentage / 
        (100 - ideal_clock_duty_cycle_percentage);
    horizontal_blank -= horizontal_blank % VESA_CHARACTER_CELL_W;

    int total_horizontal = total_hor_active_pixels + horizontal_blank;

    int actual_clock_freq_khz = (total_horizontal*1000) / scanline_khz;
    actual_clock_freq_khz -= actual_clock_freq_khz % VESA_CLOCKSTEP_KHZ;

    // missing 16, 17, 18 https://glenwing.github.io/docs/VESA-CVT-1.2.pdf
}

struct vesa_modeline vesa_modeline_from_pixel_clock(
    int width, int height,
    int overscan,
    enum vga_clocks clock
) {
    /*
    VGA has 3 clocks:
    dot8 - standard 25MHz clock, used for 4:3
    dot9 - standard 28MHz clock, used for 16:9
    some (like the Paradise and its clones) has 42 MHz
    the last one is usually not present
    */
    
    int dot_length = clock == DOT8 ? 8 : 9;
    // VESA says to round down to character cell
    width -= width % dot_length;
    height -= height % VESA_CHARACTER_CELL_W;

    int total_hor_active_pixels = width + 2 * overscan;
    int horizontal_blank;
    int total_horizontal;

    int pixel_clock_hz;
    switch (clock) {
        case UNSPEC:
        case DOT8:
            pixel_clock_hz = 25 * 1000000;
            break;
        case DOT9:
            pixel_clock_hz = 28 * 1000000;
            break;
        case PARADISE_42:
            pixel_clock_hz = 42 * 1000000;
    }

    int vertical_sync_backporch;
    int vertical_backporch;
    int total_vert;
    int final_refresh_rate = 0;

    struct vesa_modeline out = {
        .clock_type = clock,
        .horizontal = width,
        
        .vertical = height,
        .vertical_fporch = 2,

        .overscan = 0,
    };
}