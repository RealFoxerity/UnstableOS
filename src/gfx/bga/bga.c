// see https://github.com/bochs-emu/Bochs/blob/master/bochs/iodev/display/vga.h

#include "kernel.h"
#include "lowlevel.h"
#include "pci/pci.h"
#include "bga.h"
#include "gfx/vbe.h"
#include "gfx.h"
#define BGA_VENDOR_ID 0x1234
#define BGA_PRODUCT_ID 0x1111

#define BGA_INDEX_PORT 0x1CE
#define BGA_DATA_PORT 0x1CF

// 0xb0c2 introduced linear framebuffer and >8 bpp
// 0xb0c3 introduced getcaps
#define BGA_MINIMUM_SUPPORTED_VERSION 0xb0c3

// we can reuse the VBE gfx functions, as we'd be doing basically the same thing
static struct VBE_modes_list bga_faked_vbe_mode = {
    .mode_num = 0xFFFF,
    .next     = NULL,
    .info     = {
        .attributes = {
            .color_mode = 1,
            .linear_framebuffer = 1,
            .supported = 1
        },
        .memory_model = VBE_MEMORY_MODEL_DIRECT,


        // these 3 changed in bga_init
        .pitch = 0,
        .width = 0,
        .height = 0,

        // direct color options
        .bpp = 32,

        .red_mask   = 8,
        .green_mask = 8,
        .blue_mask  = 8,

        .red_position = 16,
        .green_position = 8,
        .blue_position = 0,
    }
};


void bga_set_res(unsigned int x, unsigned int y);

static unsigned int bga_max_xres   = 0;
static unsigned int bga_max_yres   = 0;
static void * bga_framebuffer_phys = NULL;

char bga_init(struct pci_device device) {
    struct pci_bar framebuffer_bar = pci_get_bar(device.bus, device.device, device.function, 0);
    bga_framebuffer_phys = framebuffer_bar.base_address;

    uint16_t bga_version = bga_read_register(BGA_REG_ID);
    if (bga_version < BGA_MINIMUM_SUPPORTED_VERSION) {
        kprintf("bga: Device version older than minimum supported!\n");
        return -1;
    }

    bga_write_register(BGA_REG_ENABLE, BGA_GETCAPS);
    bga_max_xres = bga_read_register(BGA_REG_XRES);
    bga_max_yres = bga_read_register(BGA_REG_YRES);

    if (bga_max_xres > BGA_MAX_ALLOWABLE_XRES) bga_max_xres = BGA_MAX_ALLOWABLE_XRES;
    if (bga_max_yres > BGA_MAX_ALLOWABLE_YRES) bga_max_yres = BGA_MAX_ALLOWABLE_YRES;

    int max_bpp  = bga_read_register(BGA_REG_BPP);
    if (max_bpp < 32) {
        bga_write_register(BGA_REG_ENABLE, 1 | BGA_LFB_ENABLE);
        kprintf("bga: Device doesn't support at least 32 bpp");
        return -1;
    }
    bga_set_res(bga_max_xres, bga_max_yres);

    return 0;
}

void bga_set_res(unsigned int x, unsigned int y) {
    if (x > bga_max_xres || y > bga_max_yres) {
        kprintf("bga: Warning: tried to set a resolution too high\n");
        return;
    }
    kprintf("bga: Setting resolution %ux%u\n", x, y);

    spinlock_acquire_interruptible(&gfx_spinlock);


    // to allow changing resolutions without locking the framebuffer (avoiding overflow)
    if (display_width > x)  display_width = x;
    if (display_height > y) display_height = y;

    bga_write_register(BGA_REG_ENABLE, 0);
    bga_write_register(BGA_REG_XRES, x);
    bga_write_register(BGA_REG_YRES, y);

    // 32 bpp is the easiest mode to work with
    // it's supported since 0xb0c2 (which is the lowest we support)
    // considering there's 0 benefit choosing anything else
    // and having a single bpp is easier to implement
    // i chose to just hardcode 32 bpp and not implement accesses to others
    bga_write_register(BGA_REG_BPP, 32);

    // bga is fast enough that it doesn't require double buffering
    //gfx_realloc_back_framebuffer(bga_max_xres, bga_max_yres);
    gfx_unmap_back_framebuffer();

    gfx_remap_framebuffer(bga_framebuffer_phys, bga_max_xres * bga_max_yres * 4, 0);

    bga_write_register(BGA_REG_ENABLE, 1 | BGA_LFB_ENABLE);

    // assuming BGA has a larger/the same framebuffer size as VBE, which it should
    extern size_t vbe_framebuffer_size;
    vbe_framebuffer_size = bga_max_xres * bga_max_yres * 4;

    // having these numbers wrong just means broken video for a short while so no framebuffer locking needed
    // in case of pitch, nothing happens
    // in case of display_width and _height, since that only controls framebuffer access,
    // and we can never have larger resolution than framebuffer has space for (from GETCAPS) nothing happens as well
    bga_faked_vbe_mode.info.pitch  = bga_max_xres * 4;
    bga_faked_vbe_mode.info.width  = bga_max_xres;
    bga_faked_vbe_mode.info.height = bga_max_yres;
    bga_faked_vbe_mode.info.width  = bga_max_xres * 4;

    extern const struct VBE_modes_list * vbe_current_mode;
    vbe_current_mode = &bga_faked_vbe_mode;

    extern struct gfx_funcs vbe_funcs;
    current_video_funcs = &vbe_funcs;

    display_width  = bga_max_xres;
    display_height = bga_max_yres;


    spinlock_release(&gfx_spinlock);
}

uint16_t bga_read_register(enum bga_register_indices reg) {
    outw(BGA_INDEX_PORT, reg);
    return inw(BGA_DATA_PORT);
}

void bga_write_register(enum bga_register_indices reg, uint16_t value) {
    outw(BGA_INDEX_PORT, reg);
    outw(BGA_DATA_PORT, value);
}