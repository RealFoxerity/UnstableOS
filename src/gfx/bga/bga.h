#ifndef BGA_H
#define BGA_H
#include <stdint.h>

#define BGA_LFB_ENABLE 0x40 // for BGA_REG_ENABLE
#define BGA_GETCAPS 0x02 // for BGA_REG_ENABLE

enum bga_register_indices {
    BGA_REG_ID,
    BGA_REG_XRES,
    BGA_REG_YRES,
    BGA_REG_BPP,
    BGA_REG_ENABLE, // 0 = disable, 1 = enable
    BGA_REG_BANK,
    BGA_REG_VIRT_XRES,
    BGA_REG_VIRT_YRES,
    BGA_REG_X_OFFSET,
    BGA_REG_Y_OFFSET,
};

void bga_write_register(enum bga_register_indices reg, uint16_t value);
uint16_t bga_read_register(enum bga_register_indices reg);
void bga_set_bpp(unsigned char bpp);

#endif