#include "../../libc/src/include/UnstableOS/devs.h"
#include "dev_ops.h"
#include "gfx.h"
#include <errno.h>

#include "string.h"

off_t framebuffer_seek(file_descriptor_t *file, off_t off, int whence) {
#ifdef FB_ACCESS_CALLS_GFX_API
    size_t max_off = display_width * display_height * 4; // internally using 32 bpp
#else
    size_t max_off = framebuffer_size;
#endif


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

#ifdef FB_ACCESS_CALLS_GFX_API
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

        pixels[i] = current_video_funcs->read_framebuffer(x, y);
    }
    file->off += i * 4;
    return i * 4;
}

ssize_t framebuffer_write(file_descriptor_t *file, const void *buf, size_t count) {
    size_t max_off = display_width * display_height * 4; // internally using 32 bpp
    if (file->off >= max_off) return 0;

    count /= 4; // 32bpp
    const uint32_t * pixels = buf;

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
#else
ssize_t framebuffer_pread(file_descriptor_t *file, void *buf, size_t count, off_t offset) {
    if (offset < 0) return -EINVAL;

    spinlock_acquire_interruptible(&framebuffer_lock);
    size_t max_off = framebuffer_size;
    if (offset >= max_off) {
        spinlock_release(&framebuffer_lock);
        return 0;
    }

    if (offset + count >= max_off) {
        count = max_off - offset;
    }

    memcpy(buf, LINEAR_FRAMEBUFFER_START + offset, count);

    spinlock_release(&framebuffer_lock);
    return count;
}

ssize_t framebuffer_pwrite(file_descriptor_t *file, const void *buf, size_t count, off_t offset) {
    if (offset < 0) return -EINVAL;

    spinlock_acquire_interruptible(&framebuffer_lock);
    size_t max_off = framebuffer_size;
    if (offset >= max_off) {
        spinlock_release(&framebuffer_lock);
        return 0;
    }

    if (offset + count >= max_off) {
        count = max_off - offset;
    }

    memcpy(LINEAR_FRAMEBUFFER_START + offset, buf, count);

    spinlock_release(&framebuffer_lock);
    return count;
}
#endif
/*
long framebuffer_ioctl(file_descriptor_t *file, unsigned long cmd, void * arg) {
    return -EINVAL;
}
*/
struct dev_operations framebuffer_ops = {
    .pread = framebuffer_pread,
    .pwrite = framebuffer_pwrite,
    .seek = framebuffer_seek,
    //.ioctl = framebuffer_ioctl,
};

void framebuffer_register() {
    dev_register_ops(GET_DEV(DEV_MAJ_FB, 0), &framebuffer_ops);
}