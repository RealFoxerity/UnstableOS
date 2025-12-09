#include "../include/kernel.h"
#include "../include/fs/fs.h"
#include "../include/errno.h"

superblock_t * kernel_superblocks[FS_LIMIT_KERNEL] = {0};
spinlock_t kernel_superblock_lock = {0};

superblock_t * get_superblock(dev_t dev) {
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

long sys_umount(dev_t device) { // TODO: check whether this synchronization actually makes sense in multicore setting :P
    superblock_t * sb = get_superblock(device);
    if (sb == NULL) return ENOENT;

    kassert(sb->mountpoint != NULL);
    kassert(sb->mountpoint->instances > 0);

    spinlock_acquire(&kernel_inode_lock);
    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        if (kernel_inodes[i] && kernel_inodes[i]->device == sb->device
            && (kernel_inodes[i]->instances > kernel_inodes[i]->is_mountpoint ? 1 : 0)) {
                spinlock_release(&kernel_inode_lock);    
                return EBUSY;
        }

    }
    spinlock_acquire(&sb->lock);
    sb->is_mounted = 0;
    
    sb->mountpoint->instances--;
    sb->mountpoint->is_mountpoint = 0;

    spinlock_release(&sb->lock);
    spinlock_release(&kernel_inode_lock);
    return 0;
}

long sys_mount(const char * dev_path, const char * mount_path, unsigned short options) {

}