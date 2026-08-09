#include "gfx/vbe.h"
#include "kernel.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include <string.h>
#include <errno.h>
#include <bits/ioctl/fb_ioctl.h>


extern const struct VBE_modes_list * vbe_current_mode;
extern size_t vbe_mode_count;
extern struct VBE_modes_list * vbe_modes_list;

static void vbe_to_fbinfo(struct fb_info * info, const struct VBE_modes_list * mode) {
    info->id = mode->mode_num;
    info->type =
        mode->info.attributes.linear_framebuffer ?
            FBIT_LINEAR:
            FBIT_PLANAR;
    info->pitch = mode->info.pitch;
    info->width = mode->info.width;
    info->height = mode->info.height;
    info->planes = mode->info.planes;
    info->bpp = mode->info.bpp;
    info->red_mask = mode->info.red_mask;
    info->green_mask = mode->info.green_mask;
    info->blue_mask = mode->info.blue_mask;
    info->red_position = mode->info.red_position;
    info->green_position = mode->info.green_position;
    info->blue_position = mode->info.blue_position;
}

long vbe_ioctl(unsigned long cmd, void * arg) {
    const struct VBE_modes_list * this = vbe_modes_list;
    struct fb_info *info = arg;

    switch (cmd) {
        case FB_GET_ACTIVE_MODE:
            if (!paging_check_address_range(arg, sizeof(struct fb_info), 1, 0))
                return -EFAULT;
            vbe_to_fbinfo(info, vbe_current_mode);
            return 0;
        case FB_GET_MODES:
            if (arg == NULL)
                return (long)vbe_mode_count;
            if (!paging_check_address_range(arg, sizeof(struct fb_info) * vbe_mode_count, 1, 0))
                return -EFAULT;

            for (const struct VBE_modes_list * mode = vbe_modes_list; mode != NULL; mode = mode->next) {
                vbe_to_fbinfo(info, mode);
                info++;
            }
            return 0;
        case FB_SET_MODE:
            for (this = vbe_modes_list; this != NULL; this = this->next) {
                if (this->mode_num == (uintptr_t)arg)
                    break;
            }
            if (this == NULL)
                return -EINVAL;

            spinlock_acquire(&gfx_spinlock);
            int ret = vbe_set_info(this);
            spinlock_release(&gfx_spinlock);
            return ret ? : -EIO;
        default:
            return -ENOTTY;
    }
}