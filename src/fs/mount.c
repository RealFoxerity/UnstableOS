#include "fs/fs.h"
#include "fs/vfs.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_spinlock.h"
#include <string.h>
#include <errno.h>

#include "kernel_sched.h"
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

    file_descriptor_t * file = NULL;
    kassert(open_raw_device(dev, (options&MOUNT_RDONLY) ? O_RDONLY : O_RDWR, &file) == 0);
    
    superblock_t * root_superblock = get_free_superblock();
    kassert(root_superblock);

    root_superblock->device = dev;
    root_superblock->fd = file;
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
    root_inode->instances += 2; // because pwd and root for kernel task
    spinlock_release(&root_inode->lock);

    root_mountpoint->mountpoint = root_inode;
    root_superblock->mountpoint = root_inode;

    spinlock_release(&mount_tree_lock);

    kernel_task->pwd = kernel_task->root = root_inode;

    return 0;
}

// TODO: actually do stuff with the mount_root

long mount_dev(dev_t dev, inode_t * mount_point, unsigned char type, unsigned short options) {
    kassert(root_mountpoint);
    kassert(mount_point);
    kassert(type < SUPPORTED_FS_COUNT);

    file_descriptor_t * file = NULL;
    int ret                  = open_raw_device(dev, options & MOUNT_RDONLY ? O_RDONLY : O_RDWR, &file);
    if (ret < 0) return ret;
    kassert(file);

    superblock_t * new_superblock = get_free_superblock();
    if (new_superblock == NULL) {
        close_file(file);
        return -ENOMEM;
    }

    new_superblock->device        = dev;
    new_superblock->fd            = file;
    new_superblock->fs_type       = type;
    new_superblock->mountpoint    = mount_point;
    new_superblock->mount_options = options;
    new_superblock->funcs         = fs_operations[type];
    kassert(new_superblock->funcs->lookup);

    if (new_superblock->funcs->fs_init && new_superblock->funcs->fs_init(new_superblock) != 0) {
        close_file(file);
        new_superblock->is_mounted = 0;
        return -EINVAL;
    }

    spinlock_acquire(&mount_point->lock);
    // mountpoints have an implicit +1 in the instance counter
    __atomic_add_fetch(&mount_point->instances, 1, __ATOMIC_RELAXED);
    mount_point->is_mountpoint   = 1;
    mount_point->device          = dev;
    mount_point->next_superblock = new_superblock;
    spinlock_release(&mount_point->lock);

    /*
    spinlock_acquire(&mount_tree_lock);

    spinlock_release(&mount_tree_lock);
    */
    return 0;
}

long sys_mount(const char * dev_path, const char * mount_path, unsigned char type, unsigned short options) {
    if (type >= SUPPORTED_FS_COUNT) return -ENODEV;

    inode_t * mount_inode = NULL, * dev_inode = NULL;
    int ret = openat_inode(current_process->pwd, mount_path, O_RDONLY | O_DIRECTORY, 0, &mount_inode);
    if (ret < 0 || mount_inode == NULL) return ret;

    switch (type) {
        case FS_DEVFS: // devfs doesn't require a device
            ret = mount_dev(-1, mount_inode, type, options);
            close_inode(mount_inode);
            return ret;
        default:
            ret = openat_inode(current_process->pwd,
                dev_path,
                O_RDONLY | ((options & MOUNT_RDONLY) ? 0 : O_WRONLY),
                0,
                &dev_inode);
            if (ret < 0 || dev_inode == NULL) {
                if (dev_inode != NULL ) close_inode(dev_inode);
                return ret;
            }
            if (S_ISDIR(dev_inode->mode) || S_ISFIFO(dev_inode->mode)) {
                close_inode(dev_inode);
                return -ENODEV;
            }
            ret = mount_dev(dev_inode->device, mount_inode, type, options);
            close_inode(dev_inode);
            close_inode(mount_inode);
            return ret;
    }
}