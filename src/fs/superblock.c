#include "../include/kernel.h"
#include "../include/fs/fs.h"
#include "../include/errno.h"
#include "../include/mm/kernel_memory.h"
#include "../../libc/src/include/string.h"

superblock_t ** kernel_superblocks;
spinlock_t kernel_superblock_lock = {0};

void init_superblocks() {
    kernel_superblocks = kalloc(sizeof(superblock_t *) * FS_LIMIT_KERNEL);
    kassert(kernel_superblocks);
    memset(kernel_superblocks, 0, sizeof(superblock_t *) * FS_LIMIT_KERNEL);
}

superblock_t * get_free_superblock() {
    kassert(kernel_superblocks);
    superblock_t * ret = NULL;
    spinlock_acquire(&kernel_superblock_lock);
    for (int i = 0; i < FS_LIMIT_KERNEL; i++) {
        if (kernel_superblocks[i] && !kernel_superblocks[i]->is_mounted) {
            found:
            memset(kernel_superblocks[i], 0, sizeof(superblock_t));
            kernel_superblocks[i]->is_mounted = 1;
            ret = kernel_superblocks[i];
            break;
        }
        if (!kernel_superblocks[i]) {
            kernel_superblocks[i] = kalloc(sizeof(superblock_t));
            goto found;
        }
    }
    spinlock_release(&kernel_superblock_lock);
    return ret;
}

superblock_t * get_superblock(dev_t dev) {
    kassert(kernel_superblocks);
    spinlock_acquire(&kernel_superblock_lock);
    for (int i = 0; i < FS_LIMIT_KERNEL; i++) {
        if (kernel_superblocks[i] != NULL && kernel_superblocks[i]->is_mounted && kernel_superblocks[i]->device) {
            spinlock_release(&kernel_superblock_lock);
            return kernel_superblocks[i];        
        }
    }
    spinlock_release(&kernel_superblock_lock);
    return NULL;
}