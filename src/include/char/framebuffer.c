#include "devs.h"
#include "dev_ops.h"
#include "gfx.h"
#include <errno.h>

/*
 * we don't allow direct VRAM access primarily because of:
 * different linear addresses for VRAM
 * different bitmasks, bpp
 * I'm too lazy to pass the framebuffer structure for video info to the user via ioctl
 * all of these are easily solvable, but i don't know how to properly implement them for pure VGA
 * TODO: Very low priority: Add support in the far future?
 */

ssize_t framebuffer_read(file_descriptor_t *file, void *buf, size_t count) {
    // assuming file offset isn't fucked up
    size_t max_off = display_width * display_height * 4; // internally using 32 bpp
    if (file->off >= max_off) return 0;

    count /= 4; // 32bpp
    uint32_t * pixels = buf;

    unsigned int i = 0;
    for (; i < count; i++) {
        unsigned int x = (file->off + i) % display_width;
        unsigned int y = (file->off + i) / display_width;
        if (y >= display_height) break;

        pixels[i] = current_video_funcs->read_pixel(x, y);
    }
    file->off += i * 4;
    return i * 4;
}

ssize_t framebuffer_write(file_descriptor_t *file, const void *buf, size_t count) {
    size_t max_off = display_width * display_height * 4; // internally using 32 bpp
    if (file->off >= max_off) return 0;

    count /= 4; // 32bpp
    uint32_t * pixels = buf;

    unsigned int i = 0;
    for (; i < count; i++) {
        unsigned int x = (file->off + i) % display_width;
        unsigned int y = (file->off + i) / display_width;
        if (y >= display_height) break;

        current_video_funcs->write_pixel_buffered(x, y, pixels[i], 0);
        current_video_funcs->swap_region(x, y, x, y);
    }
    file->off += i * 4;
    return i * 4;
}

off_t framebuffer_seek(file_descriptor_t *file, off_t off, int whence) {
    size_t max_off = display_width * display_height * 4;

    switch (whence) {
        case SEEK_SET:
            if (off < 0) return -EINVAL;
            if (off > max_off) return -EINVAL;
            return file->off = off;
        case SEEK_CUR:
            if (file->off + off > file->off && off < 0) return -EINVAL; // underflow - negative offset
            if (file->off + off < file->off && off > 0) return -E2BIG; // overflow

            if (file->off + off > max_off) return -EINVAL;
            return file->off += off;
        case SEEK_END:
            if (off > 0) return -EINVAL;
            if (off == 0) return file->off = max_off;
            if (-off <= file->off) return file->off = file->off - off;
            return -EINVAL; // negative offset
        default:
            return -EINVAL;
    }
}

/*
long framebuffer_ioctl(file_descriptor_t *file, unsigned long cmd, void * arg) {
    return -EINVAL;
}
*/
struct dev_operations framebuffer_ops = {
    .read = framebuffer_read,
    .write = framebuffer_write,
    .seek = framebuffer_seek,
    //.ioctl = framebuffer_ioctl,
};

void framebuffer_register() {
    dev_register_ops(GET_DEV(DEV_MAJ_FB, 0), &framebuffer_ops);
}