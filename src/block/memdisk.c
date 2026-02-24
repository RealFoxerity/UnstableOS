#include "../include/kernel.h"
#include "../include/mm/kernel_memory.h"
#include "../include/devs.h"
#include "../include/fs/fs.h"
#include "../include/block/memdisk.h"
#include "../include/errno.h"
#include "../../libc/src/include/string.h"
#include <stddef.h>
#include <stdint.h>

spinlock_t memdisk_lock = {0};
memdisk_t memdisks[MEMDISK_LIMIT_KERNEL] = {0};

static int get_free_memdisk() { // call with locked memdisk_lock
    int selected = -1;
    for (int i = 0; i < MEMDISK_LIMIT_KERNEL; i++) {
        if (!memdisks[i].used) {
            memset(&memdisks[i], 0, sizeof(memdisk_t));
            memdisks[i].used = 1;
            selected = i;
            break;
        }
    }
    return selected;
}

long memdisk_deinit(dev_t dev) { // TODO: rewrite with some better locking, this could theoretically race to mem->busy 
    if (MAJOR(dev) != DEV_MAJ_MEM) return EINVAL;
    if (MINOR(dev) > DEV_MEM_MEMDISK3) return EINVAL;

    spinlock_acquire(&memdisk_lock);
    memdisk_t * mem = &memdisks[MINOR(dev)];
    if (!mem->used) {
        spinlock_release(&memdisk_lock);
        return EINVAL;
    }
    if (mem->busy) {
        spinlock_release(&memdisk_lock);
        return EBUSY;
    } else {
        mem->used = 0;
    }
    if (mem->is_allocated) paging_unmap(mem->start_addr, mem->size);

    spinlock_release(&memdisk_lock);

    return 0;
}

dev_t memdisk_alloc() { // allocates a memdisk of DEFAULT_MEMDISK_SIZE size
    spinlock_acquire(&memdisk_lock);
    int new_memdisk = get_free_memdisk();
    if (new_memdisk == -1) {
        spinlock_release(&memdisk_lock);
        return -1;
    }

    memdisk_t * mem = &memdisks[new_memdisk];
    mem->is_allocated = 1;
    mem->start_addr = MEMDISKS_BASE + new_memdisk * DEFAULT_MEMDISK_SIZE;
    mem->size = DEFAULT_MEMDISK_SIZE;
    spinlock_release(&memdisk_lock);
    kprintf("Allocated a new memdisk of size %u maj %d min %d\n", DEFAULT_MEMDISK_SIZE, DEV_MAJ_MEM, DEV_MEM_MEMDISK0 + new_memdisk);
    return GET_DEV(DEV_MAJ_MEM, DEV_MEM_MEMDISK0 + new_memdisk);
}

dev_t memdisk_from_range(void * vaddr, size_t n) {
    spinlock_acquire(&memdisk_lock);
    int new_memdisk = get_free_memdisk();
    if (new_memdisk == -1) {
        spinlock_release(&memdisk_lock);
        return -1;
    }

    memdisks[new_memdisk].start_addr = vaddr;
    memdisks[new_memdisk].size = n;
    spinlock_release(&memdisk_lock);
    kprintf("Mapped a new memdisk vaddr 0x%p - 0x%p maj %d min %d\n", vaddr, vaddr+n, DEV_MAJ_MEM, DEV_MEM_MEMDISK0 + new_memdisk);
    return GET_DEV(DEV_MAJ_MEM, DEV_MEM_MEMDISK0 + new_memdisk);
}

ssize_t memdisk_read_internal(dev_t dev, size_t seek, void * s, size_t n) {
    if (MAJOR(dev) != DEV_MAJ_MEM) return EINVAL;
    if (MINOR(dev) > DEV_MEM_MEMDISK3) return EINVAL;

    if (!memdisks[MINOR(dev)].used) return EINVAL;
    if (seek > INT32_MAX) return E2BIG;
    if (seek >= memdisks[MINOR(dev)].size) return 0;

    __atomic_add_fetch(&memdisks[MINOR(dev)].busy, 1, __ATOMIC_RELAXED);

    size_t len = 0;
    for (unsigned char * i = memdisks[MINOR(dev)].start_addr + seek; i < (unsigned char *)memdisks[MINOR(dev)].start_addr + memdisks[MINOR(dev)].size && len < n; i++, len++) {
        ((unsigned char *)s)[len] = *i;
    }

    __atomic_sub_fetch(&memdisks[MINOR(dev)].busy, 1, __ATOMIC_RELAXED);

    return len++; // was working with indices before
}

ssize_t memdisk_write_internal(dev_t dev, size_t seek, const void * s, size_t n) {
    if (MAJOR(dev) != DEV_MAJ_MEM) return EINVAL;
    if (MINOR(dev) > DEV_MEM_MEMDISK3) return EINVAL;

    if (!memdisks[MINOR(dev)].used) return EINVAL;
    if (seek > INT32_MAX) return E2BIG;
    if (seek > memdisks[MINOR(dev)].size) return EFBIG;
    if (seek == memdisks[MINOR(dev)].size) return 0;

    __atomic_add_fetch(&memdisks[MINOR(dev)].busy, 1, __ATOMIC_RELAXED);

    size_t len = 0;
    for (unsigned char * i = memdisks[MINOR(dev)].start_addr + seek; i < (unsigned char *)memdisks[MINOR(dev)].start_addr + memdisks[MINOR(dev)].size && len < n; i++, len++) {
        *i = ((unsigned char *)s)[len];
    }

    __atomic_sub_fetch(&memdisks[MINOR(dev)].busy, 1, __ATOMIC_RELAXED);

    return len++;
}



ssize_t memdisk_read(file_descriptor_t * fd, void * s, size_t n) {
    kassert(fd);
    kassert(s);
    if (n == 0) return 0;
    if (n > INT32_MAX) n = INT32_MAX;

    ssize_t read = memdisk_read_internal(fd->inode->device, fd->off, s, n);
    if (read > 0) fd->off += read;
    return read;
}

ssize_t memdisk_write(file_descriptor_t * fd, const void * s, size_t n) {
    kassert(fd);
    kassert(s);
    if (n == 0) return 0;
    if (n > INT32_MAX) n = INT32_MAX;
    
    ssize_t write =  memdisk_write_internal(fd->inode->device, fd->off, s, n);
    if (write > 0) fd->off += write;
    return write;
}

off_t memdisk_seek(file_descriptor_t * fd, off_t off, int whence) { // offsets over the file size are handled during read/write, assumes file descriptor locked beforehand
    kassert(fd);

    dev_t dev = fd->inode->device;

    if (MAJOR(dev) != DEV_MAJ_MEM) return EINVAL;
    if (MINOR(dev) > DEV_MEM_MEMDISK3) return EINVAL;

    memdisk_t * memdisk = &memdisks[MINOR(dev)];
    if (!memdisk->used) return EINVAL;

    switch (whence) {
        case SEEK_SET:
            if (off < 0) return EINVAL;
            return fd->off = off;
        case SEEK_CUR:
            if (fd->off + off > fd->off && off < 0) return EINVAL; // underflow - negative offset
            if (fd->off + off < fd->off && off > 0) return E2BIG; // overflow

            return fd->off = fd->off + off;
        case SEEK_END:
            if (off >= 0) {
                if (fd->off + off > fd->off && off < 0) return EINVAL; // underflow - negative offset
                if (fd->off + off < fd->off && off > 0) return E2BIG; // overflow
                return fd->off = memdisk->size + off;
            }
            else if (off < 0 && -off <= fd->off) return fd->off = fd->off - off;
            return EINVAL; // negative offset
        default:
            return EINVAL;
    }
}