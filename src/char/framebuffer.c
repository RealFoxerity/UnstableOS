#include <UnstableOS/devs.h>
#include "dev_ops.h"
#include "gfx.h"
#include <errno.h>

#include "string.h"
#include "bits/ioctl/tty_ioctl.h"

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

long framebuffer_ioctl(file_descriptor_t *file, unsigned long cmd, void * arg) {
    if (!current_video_funcs->ioctl)
        return -ENOTTY;
    if (__IOCTL_DEV(cmd) != DEV_MAJ_FB)
        return -EINVAL;
    return current_video_funcs->ioctl(file, cmd, arg);
}

#include <sys/mman.h>
long framebuffer_mmap(inode_t * inode, int prot, off_t off, void * start, size_t len) {
    if(off % PAGE_SIZE || off < 0)
        return -EINVAL;

    len += PAGE_SIZE - 1;
    len &= ~(PAGE_SIZE - 1);

    if (len + off > LINEAR_FRAMEBUFFER_MAX_SIZE)
        len = LINEAR_FRAMEBUFFER_MAX_SIZE - off;
    len /= PAGE_SIZE;
    off /= PAGE_SIZE;
    int mapping_flags = 0;
    if (prot)
        mapping_flags |= PTE_PDE_PAGE_USER_ACCESS;
    if (prot & PROT_WRITE)
        mapping_flags |= PTE_PDE_PAGE_WRITABLE;

    for (size_t i = 0; i < len; i++) {
        void * phys = paging_virt_addr_to_phys(LINEAR_FRAMEBUFFER_START + (i + off) * PAGE_SIZE);
        if (!phys)
            return 0;
        paging_map_phys_addr(phys, start + i*PAGE_SIZE, mapping_flags);
    }
    return 0;
}


struct dev_operations framebuffer_ops = {
    .pread = framebuffer_pread,
    .pwrite = framebuffer_pwrite,
    .seek = framebuffer_seek,
    .ioctl = framebuffer_ioctl,
    .mmap = framebuffer_mmap
};

void framebuffer_register() {
    dev_register_ops(GET_DEV(DEV_MAJ_FB, 0), &framebuffer_ops);
}