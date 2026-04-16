#include <stdint.h>
#include "include/kernel.h"
#include "include/lowlevel.h"
#include "include/mm/kernel_memory.h"

#include "include/vga.h"
#include "include/vga/vga_funcs.h"
#include "include/vga/vga_modelines.h"

#include "../libc/src/include/string.h"


char vga_pixels_per_address = 8; // set by individual modes to correctly set the offset CRTC register
// note that "pixels_per_address" actually means pixels per all planes combined
// so chained mode with 1 byte = 1 pixel actually has ppa 4 because the byte
// is all 4 planes combined into one

char vga_scan_doubling = 0; // set by individual modes, repeats every scan line to get e.g. 200->400 output

static char vga_addressing_mode = 0; // set by individual modes, 0 = byte, 1 = word, 2 = dw

/*
uint8_t vga_rdattr(uint8_t index) {
    inb(VGA_INPUT_STATUS_1_REGISTER);
    outb(VGA_AC_REG, index);
    return inb(VGA_AC_REG);
}
*/

void vga_wreg(uint16_t data_reg, uint8_t index, uint8_t data) {
    //kprintf("wr %hhx to %hx idx %hhx\n", data, data_reg, index);
    outb(data_reg-1, index);
    outb(data_reg, data);
}

void vga_disable_scan() {
    vga_wreg(VGA_CRTC_DATA_REG, VGA_CRTC_MODE_CONTROL, 0);
}

void vga_enable_scan() {
    /*
    Most of the bits of index the mode control register are legacy
    bit 7 - sync enable
    bit 6 - Word(0) / Byte(1) addressing, we do byte because it's easier to work with
    bit 5 - address wrap select - basically enables bit 15 of address counter
    bit 4 - empty
    bit 3 - memory address clock divider by 2 - not needed in modern days, sets refresh frequency
    bit 2 - scan line clock divider by 2 - doubles the vertical resolution (to 2048)
        note that just doubles pixels, doesn't actually increase resolution
    bit 1 - map display address 14 - enables bit 14 of address counter
    bit 0 - map display address 13 - enables bit 13 of address counter
    */
    if (vga_addressing_mode == VGA_AM_WORD || vga_addressing_mode == VGA_AM_DWORD)
        vga_wreg(VGA_CRTC_DATA_REG, VGA_CRTC_MODE_CONTROL, 0b10100011);
    else
        vga_wreg(VGA_CRTC_DATA_REG, VGA_CRTC_MODE_CONTROL, 0b11100011);
}

void vga_wrattr(uint8_t index, uint8_t data) {
    inb(VGA_INPUT_STATUS_1_REGISTER); // force resets the attribute register to index phase

    outb(VGA_AC_REG, index);
    outb(VGA_AC_REG, data);
}

// 9 dot clock theoretically would allow higher resolutions
// however either my code is bad, or qemu doesn't support it
//#define VGA_USE_DOT9

#ifdef VGA_USE_DOT9
#define VGA_DOT_DIV 9
#else
#define VGA_DOT_DIV 8
#endif

void vga_load_timings(struct vesa_modeline timings, int actual_width) {
    // note: dots can be though of as memory fetch "ticks"
    // note2: "0 |" on somes because clangd in vscode keeps inserting type hints and it's annoying

    // To be nice to the monitor
    vga_disable_scan();

    // doing this inside the driver makes it easier for other code to specify modesettings
    timings.horizontal_bporch -= timings.overscan * 2;
    timings.vertical_fporch -= timings.overscan;
    timings.vertical_bporch -= timings.overscan;

    int total_horizontal_dots =
        timings.overscan +
        timings.horizontal +
        timings.overscan +
        timings.horizontal_fporch +
        timings.horizontal_sync +
        timings.horizontal_bporch;
    total_horizontal_dots /= VGA_DOT_DIV;

    // -1 to fit inside the total horizon, -1 because zero based
    int end_horizonal_blanking = total_horizontal_dots - 2;
    int start_horizontal_retrace_dots =
        timings.overscan +
        timings.horizontal +
        timings.overscan +
        timings.horizontal_fporch;
    start_horizontal_retrace_dots /= VGA_DOT_DIV;
    
    int total_vertical_scanlines = 
        timings.overscan +
        timings.vertical +
        timings.overscan +
        timings.vertical_fporch +
        timings.vertical_sync +
        timings.vertical_bporch;
    total_vertical_scanlines--;

    int start_vertical_retrace_scanlines =
        timings.vertical +
        timings.overscan +
        timings.vertical_fporch;

    if (total_horizontal_dots > UINT8_MAX)
        kprintf("VGA: Warning: Horizontal timings exceed register size!\n");

    if (total_vertical_scanlines >= 1 << 10)
        kprintf("VGA: Warning: Vertical timings exceed register sizes!\n");

    // set sync polarity
    outb(VGA_MISC_OUT_REG_WR, 0 |
        ((!timings.vsync_polarity) << 7) | // positive = 0
        ((!timings.hsync_polarity) << 6) |
        (timings.clock_type << 2) |
        0b10   | // address enable
        0b01     // sets 0x3DX for CRT addresses
    );
    

    /*****************************************************/
    // horizontal sync setup
    /*****************************************************/

    // total dots of a scanline -5 for some reason
    vga_wreg(VGA_CRTC_DATA_REG, 0x0, total_horizontal_dots - 5);

    // horizontal display end - last pixel's dot
    vga_wreg(VGA_CRTC_DATA_REG, 0x1, timings.horizontal / VGA_DOT_DIV - 1);

    // horizontal blanking start - first blank
    vga_wreg(VGA_CRTC_DATA_REG, 0x2, timings.horizontal / VGA_DOT_DIV);

    // EVRA bit and end horizontal blanking
    vga_wreg(VGA_CRTC_DATA_REG, 0x3, 0x80 |
        (end_horizonal_blanking & 0x1F)
    );

    // start of horizontal retrace (horizontal sync)
    vga_wreg(VGA_CRTC_DATA_REG, 0x4, start_horizontal_retrace_dots);

    // end horizontal blanking bit 5 and end horizontal retrace
    vga_wreg(VGA_CRTC_DATA_REG, 0x5,
        ((end_horizonal_blanking & 0x20) << 2) | 
        ((start_horizontal_retrace_dots + timings.horizontal_sync / VGA_DOT_DIV) & 0x1F)
    );

    
    /*****************************************************/
    // vertical sync setup
    /*****************************************************/

    // total amount of scanlines
    vga_wreg(VGA_CRTC_DATA_REG, 0x6, total_vertical_scanlines);

    // overflows from various parts
    vga_wreg(VGA_CRTC_DATA_REG, 0x7, 0 |
        ((start_vertical_retrace_scanlines & 0x200) >> 2) | // bit 9 of vertical retrace start
        (((timings.vertical-1) & 0x200) >> 3) |             // bit 9 of vertical display end
        ((total_vertical_scanlines & 0x200) >> 4) |         // bit 9 of total vertical
        0x10 |                                              // bit 8 of line compare, we don't do splitscreen
        ((timings.vertical & 0x100) >> 5) |                 // bit 8 of vertical blanking start
        ((start_vertical_retrace_scanlines & 0x100) >> 6) | // bit 8 of vertical retrace start
        (((timings.vertical-1) & 0x100) >> 7) |             // bit 8 of vertical display end
        ((total_vertical_scanlines & 0x100) >> 8)           // bit 8 of total vertical
    );

    vga_wreg(VGA_CRTC_DATA_REG, 0x9, 0 |
        (vga_scan_doubling ? 0x80 : 0) |                    // scanline doubling
        0x40 |                                              // bit 9 of line compare
        ((timings.vertical & 0x200) >> 4)                   // bit 9 of vertical blanking start
    );

    // start of vertical retrace (vertical sync)
    vga_wreg(VGA_CRTC_DATA_REG, 0x10, start_vertical_retrace_scanlines);

    // (un)protect bit, end of vertical retrace
    vga_wreg(VGA_CRTC_DATA_REG, 0x11, (start_vertical_retrace_scanlines + timings.vertical_sync) & 0x0F);
    
    // vertical display end
    vga_wreg(VGA_CRTC_DATA_REG, 0x12, timings.vertical - 1);

    // data amount per scan line 
    // see http://www.osdever.net/FreeVGA/vga/crtcreg.htm reg 13
	vga_wreg(VGA_CRTC_DATA_REG, 0x13, actual_width / (2 * vga_pixels_per_address));

    // start of vertical blanking
    // -1 because zero based and last overscan should be blank
    vga_wreg(VGA_CRTC_DATA_REG, 0x15, timings.vertical + timings.overscan - 1);

    // end of vertical blanking
    // scanline directly after the last blank one
    // note that overscan is not blank
    vga_wreg(VGA_CRTC_DATA_REG, 0x16, total_vertical_scanlines - timings.overscan + 1);

    vga_enable_scan();
}

static void vga_setup_crt_controller() {
    // sets 0x3DX for CRT addresses
    outb(VGA_MISC_OUT_REG_WR, 1);
    
    // set panning and scroll offset to 0
    vga_wreg(VGA_CRTC_DATA_REG, 8, 0);
    
    // reset text mode registers
    for (int i = 0; i <= 0xF; i++)
        vga_wreg(VGA_CRTC_DATA_REG, i, 0);

    // disable timings protection
    vga_wreg(VGA_CRTC_DATA_REG, 0x11, 0);

    // disable "splitscreen" operation by setting line compare to 0x3FF
    // bit idx 8 is set in the overflow register as a side effect of setting the resolution
    vga_wreg(VGA_CRTC_DATA_REG, 0x18, 0xFF);
}

// sets up common parameters of the graphics sequencer
// modes required to select (with examples)
//  framebuffer mappings and parity  -> VGA_SEQ_DATA_REG idx 4
//      0b00001100 - chained (linear) framebuffer, sequential access
//      0b00000100 - unchained (planar) framebuffer, sequential access
static void vga_setup_sequencer() {
    // set the character ("dot") clock mode - most resolutions are a multiple of 8
    #ifdef VGA_USE_DOT9
    vga_wreg(VGA_SEQ_DATA_REG, 1, 0);
    #else
    vga_wreg(VGA_SEQ_DATA_REG, 1, 1);
    #endif
}

void vga_reset_sequencer() {
    vga_wreg(VGA_SEQ_DATA_REG, 0, 0);
    vga_wreg(VGA_SEQ_DATA_REG, 0, 3);
}

void vga_set_addressing_mode(enum vga_addressing_modes mode) {
    if (mode == VGA_AM_DWORD)
        vga_wreg(VGA_CRTC_DATA_REG, 0x14, 0x40);
    else
        vga_wreg(VGA_CRTC_DATA_REG, 0x14, 0);
}

// sets up common parameters of the graphics controller
// modes required to select (with examples)
//  color mode (256 shift/latched) + read/write mode  -> VGA_GC_DATA_REG idx 5
//      0b00000000 for 256 shift mode (for 256 colors), read/write mode 0 (no post process)
//      0b01000000 for 256 shift mode read/write mode 0
//  memory region + color  -> VGA_GC_DATA_REG idx 6
//      0b00000101 (for 64K, graphics) is standard
//      0b00000001 (for 128K, graphics)
static void vga_setup_gc() {
    // disable plane "expansion"
    vga_wreg(VGA_GC_DATA_REG, 0, 0);
    vga_wreg(VGA_GC_DATA_REG, 1, 0);

    // disable data rotation
    vga_wreg(VGA_GC_DATA_REG, 3, 0);

    // enable all color bits
    vga_wreg(VGA_GC_DATA_REG, 8, 0xFF);
}

// sets up common parameters
// modes required to select (with examples)
//  graphic modes (text/graphics, 8bit/latched) -> idx 10
//      0b01000001 for 8 bit color, graphics
static void vga_setup_attribute_controller() {
    // enable all color planes
    vga_wrattr(0x12, 0xF);

    // disable left shifting
    vga_wrattr(0x13, 0);

    // unblock palettes
    inb(VGA_INPUT_STATUS_1_REGISTER);
    outb(VGA_AC_REG, 0x20);
}

/*
unsigned char vga_rgb_to_rgb8(unsigned char r, unsigned char g, unsigned char b) {
    r &= 0b11100000;
    g &= 0b11100000;
    g >>= 3;
    b &= 0b11000000;
    b >>= 6;
    return r | g | b;
}
*/

void vga_set_palette_entry(char idx, unsigned char rgb8) {
    if (idx >= 16) return;

    // all 8 bits are used for color
    outb(VGA_DAC_BIT_MASK_REG, 0xFF);
    outb(VGA_DAC_WRMODE_REG, idx);

    if (rgb8 == 0xFF) { // probably wanted white
        outb(VGA_DAC_DATA_REG, 0xFF);
        outb(VGA_DAC_DATA_REG, 0xFF);
        outb(VGA_DAC_DATA_REG, 0xFF);
    } else {
        outb(VGA_DAC_DATA_REG, ((rgb8 & 0b11100000) >> 2));
        outb(VGA_DAC_DATA_REG, ((rgb8 & 0b00011100) << 1));
        outb(VGA_DAC_DATA_REG, ((rgb8 & 0b00000011) << 4));
    }

    vga_wrattr(idx, idx);

    // reenable palettes
    inb(VGA_INPUT_STATUS_1_REGISTER);
    outb(VGA_AC_REG, 0x20);
}

void vga_generate_256colors() {
    inb(VGA_INPUT_STATUS_1_REGISTER);
    // disable palette use while modifying
    outb(VGA_AC_REG, 0);

    // create a 3 3 2 bit color space
    // all 8 bits are used for color
    outb(VGA_DAC_BIT_MASK_REG, 0xFF);
    outb(VGA_DAC_WRMODE_REG, 0);
    for (int i = 0; i < 255; i++) {
        outb(VGA_DAC_DATA_REG, ((i & 0b11100000) >> 2)); // R
        outb(VGA_DAC_DATA_REG, ((i & 0b00011100) << 1)); // G
        outb(VGA_DAC_DATA_REG, ((i & 0b00000011) << 4)); // B
    }

    // completely white
    outb(VGA_DAC_DATA_REG, 0xFF);
    outb(VGA_DAC_DATA_REG, 0xFF);
    outb(VGA_DAC_DATA_REG, 0xFF);
    
    
    // reenable palettes
    inb(VGA_INPUT_STATUS_1_REGISTER);
    outb(VGA_AC_REG, 0x20);
}

enum vga_modes current_vga_mode = CHAINED;
unsigned int display_width = 320;
unsigned int display_height = 200;


#define VGA_FONT_BITMAP_ADDR_OFFSET 0x0000

void vga_init_graphics() {
    // force a [half-broken] text mode so that the font bitmap is accessible
    // font bitmaps aren't accessible in graphics modes and we use them
    // for the default console font before (if) setting from filesystem

    // enable planar mode with odd/even disabled and access to all fonts
    vga_wreg(VGA_SEQ_DATA_REG, 4, 0b00000110);
    // read/write mode 0, no odd/even
    vga_wreg(VGA_GC_DATA_REG, 0x5, 0);
    // enable alphanumeric mode
    vga_wreg(VGA_GC_DATA_REG, 0x6, 0);
    // disable graphics
    vga_wrattr(10, 0b00000000);
    // font resides on plane 2
    vga_wreg(VGA_GC_DATA_REG, 4, 2);

    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 16; j++) {
            vga_font8x16[i*16 + j] = 
                VGA_PAGE_ADDR[VGA_FONT_BITMAP_ADDR_OFFSET + i*16*2 + j];
        }
    }

    // reinitialize to correct values
    vga_setup_crt_controller();
    vga_setup_sequencer();

    vga_setup_gc();
    vga_setup_attribute_controller();

    // switch to mode 12 - 640x480x4 for a nice console size
    vga_set_mode_12();

    load_font(vga_font8x8, 256, 8, 8);

    vga_clear_screen();
}