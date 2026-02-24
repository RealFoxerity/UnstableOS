#include "../include/fs/fs.h"
#include "../include/fs/vfs.h"
#include "../include/kernel.h"
#include "../include/mm/kernel_memory.h"
#include "../include/kernel_spinlock.h"
#include "../../libc/src/include/string.h"

#include "../include/kernel_sched.h"
spinlock_t mount_tree_lock = {0};
mount_tree * root_mountpoint = NULL;

long mount_root(dev_t dev, unsigned char type, unsigned short options) {
    // this is for the kernel init only, it doesn't do proper checking of multiple things, do not call post-init
    kassert(root_mountpoint == NULL);
    kassert(kernel_superblocks[0] == NULL); // lazy way of checking this is the init
    kassert(type < SUPPORTED_FS_COUNT);
    kassert(kernel_task); // to set the kernel's pwd and root
    spinlock_acquire(&mount_tree_lock);

    root_mountpoint = kalloc(sizeof(mount_tree));
    kassert(root_mountpoint);

    memset(root_mountpoint, 0, sizeof(mount_tree));
    /*
        root_mountpoint->path = kalloc(2);
        kassert(root_mountpoint->path);
        root_mountpoint->path[0] = '/';
        root_mountpoint->path[1] = '\0';
    */
    root_mountpoint->path = kalloc(1); // due to the way we handle open(), we can't have / as the mountpoint path
    kassert(root_mountpoint->path);
    root_mountpoint->path[0] = '\0';

    int device_fd = open_raw_device(dev, (options&MOUNT_RDONLY) ? O_RDONLY : O_RDWR);
    kassert(device_fd >= 0);
    
    superblock_t * root_superblock = get_free_superblock();
    kassert(root_superblock);

    root_superblock->device = dev;
    root_superblock->fd = device_fd;
    root_superblock->mount_options = options;
    root_superblock->fs_type = type;

    root_superblock->funcs = fs_operations[type];
    kassert(root_superblock->funcs->fs_init);
    kassert(root_superblock->funcs->lookup);

    kassert(root_superblock->funcs->fs_init(root_superblock) == 0);

    inode_t * root_inode = root_superblock->funcs->lookup(root_superblock, NULL, ".");
    kassert(root_inode);
    kassert(root_inode != VFS_LOOKUP_NOTDIRECTORY);
    kassert(root_inode != VFS_LOOKUP_ESCAPE);
    kassert(root_inode != VFS_LOOKUP_NOTFOUND);

    spinlock_acquire(&root_inode->lock);
    root_inode->next_superblock = root_inode->backing_superblock;
    root_inode->is_mountpoint = 1;
    root_inode->instances ++; // because pwd and root for kernel task
    spinlock_release(&root_inode->lock);

    root_mountpoint->mountpoint = root_inode;
    
    spinlock_release(&mount_tree_lock);

    kernel_task->pwd = kernel_task->root = root_inode;

    return 0;
}

long mount_dev(dev_t dev, inode_t * mount_path, unsigned char type, unsigned short options) {

}

long sys_mount(const char * dev_path, const char * mount_path, unsigned char type, unsigned short options) {

}