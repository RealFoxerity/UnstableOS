#include "bga.h"
#include "gfx/vbe.h"
#include "fs/fs.h"
#include "kernel.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include <string.h>
#include <errno.h>
#include <bits/ioctl/fb_ioctl.h>

long bga_ioctl(file_descriptor_t * file, unsigned long cmd, void * arg) {
    struct vesa_modeline *line = arg;
    struct fb_info *info = arg;
    switch (cmd) {
        case FB_GET_ACTIVE_MODE:
            if (arg == NULL)
                return -EFAULT;
            if (!paging_check_address_range(arg, sizeof(struct fb_info), 1, 0))
                return -EFAULT;
            info->id = 0;
            info->type = FBIT_LINEAR;

            info->width = __atomic_load_n(&display_width, __ATOMIC_RELAXED);
            info->height = __atomic_load_n(&display_height, __ATOMIC_RELAXED);

            info->pitch = info->width * 4;
            info->planes = 1;
            info->bpp = 32;

            info->red_mask       = 8;
            info->green_mask     = 8;
            info->blue_mask      = 8;
            info->red_position   = 16;
            info->green_position = 8;
            info->blue_position  = 0;
            return 0;
        case FB_GET_MODELINE:
            if (arg == NULL)
                return -EFAULT;
            if (!paging_check_address_range(arg, sizeof(struct vesa_modeline), 1, 0))
                return -EFAULT;
            memset(line, 0, sizeof(struct vesa_modeline));
            line->horizontal = __atomic_load_n(&display_width,  __ATOMIC_RELAXED);
            line->vertical   = __atomic_load_n(&display_height, __ATOMIC_RELAXED);
            return 0;
        case FB_SET_MODELINE:
            if (file->inode->mmaped_instances)
                return -EBUSY;
            if (arg == NULL)
                return -EFAULT;
            if (!paging_check_address_range(arg, sizeof(struct vesa_modeline), 0, 0))
                return -EFAULT;

            return bga_set_res(line->horizontal, line->vertical);
        default:
            return -ENOTTY;
    }
}