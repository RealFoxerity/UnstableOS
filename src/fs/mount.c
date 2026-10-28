#include "fs/fs.h"
#include "fs/vfs.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_spinlock.h"
#include <string.h>
#include <errno.h>

#include "kernel_sched.h"
rw_spinlock_t mount_tree_lock = {0};
mount_tree * root_mountpoint = NULL;
static unsigned long last_fsid = 0; // statvfs will require this

long mount_root(dev_t dev, unsigned char type, unsigned short options) {
    // this is for the kernel init only, it doesn't do proper checking of multiple things, do not call post-init
    kassert(root_mountpoint == NULL);
    kassert(kernel_superblocks[0] == NULL); // lazy way of checking this is the init
    kassert(type < SUPPORTED_FS_COUNT);
    kassert(kernel_task); // to set the kernel's pwd and root
    rw_spinlock_acquire_write(&mount_tree_lock);

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

    inode_t * root_inode = NULL;
    long status = root_superblock->funcs->lookup(root_superblock, NULL, ".", &root_inode,
        (options & MOUNT_RDONLY ? O_RDONLY : O_RDWR) | O_DIRECTORY);
    kassert(root_inode);
    kassert(status >= 0);

    spinlock_acquire(&root_inode->lock);
    root_inode->next_superblock = root_inode->backing_superblock;
    root_inode->is_mountpoint = 1;
    root_inode->instances += 2; // because pwd and root for kernel task
    spinlock_release(&root_inode->lock);

    root_mountpoint->mountpoint = root_inode;
    root_superblock->mountpoint = root_inode;

    rw_spinlock_release_write(&mount_tree_lock);

    kernel_task->pwd = kernel_task->root = root_inode;

    return 0;
}

// TODO: actually do stuff with the mount_root

long mount_dev(dev_t dev, inode_t * mount_point, unsigned char type, unsigned short options) {
    kassert(root_mountpoint);
    kassert(mount_point);
    kassert(type < SUPPORTED_FS_COUNT);

    // procfs folders might disappear on process death, it's not a stable mounting environment
    if (mount_point->backing_superblock && mount_point->backing_superblock->fs_type == FS_procfs)
        return -EINVAL;

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
    new_superblock->fsid          = __atomic_add_fetch(&last_fsid, 1, __ATOMIC_ACQUIRE);

    kassert(new_superblock->funcs->lookup);

    if (new_superblock->funcs->fs_init && (ret = new_superblock->funcs->fs_init(new_superblock)) != 0) {
        close_file(file);
        new_superblock->is_mounted = 0;
        return ret;
    }

    spinlock_acquire(&mount_point->lock);
    // mountpoints have an implicit +1 in the instance counter
    __atomic_add_fetch(&mount_point->instances, 1, __ATOMIC_ACQUIRE);

    mount_point->next_superblock = new_superblock;
    // ensure next_superblock is assigned before is_mountpoint so that we don't race in openat_inode
    __atomic_thread_fence(__ATOMIC_RELEASE);
    mount_point->is_mountpoint   = 1;
    spinlock_release(&mount_point->lock);

    /*
    spinlock_acquire(&mount_tree_lock);

    spinlock_release(&mount_tree_lock);
    */
    return 0;
}

long sys_mount(const char * dev_path, const char * mount_path, const char * type, unsigned short options) {
    if (!type || !mount_path) return -EFAULT;
    if (current_process->euid != 0)
        return -EPERM;

    size_t len = 0;
    type = secure_strdup(type, 10, &len);
    if (type == NULL)
        return -EFAULT;

    enum supported_filesystems fs = -1;

    for (int i = 0; i < SUPPORTED_FS_COUNT; i++) {
        if (!strcmp(type, fs_names[i])) {
            fs = i;
            break;
        }
    }

    kfree((void*)type);
    if (fs == -1)
        return -ENODEV;

    inode_t * mount_inode = NULL, * dev_inode = NULL;
    int ret = openat_inode((inode_t*)AT_FDCWD, mount_path, O_RDONLY | O_DIRECTORY | O_NOXDEV, 0, &mount_inode, 0);
    if (ret < 0 || mount_inode == NULL) return ret;

    if (!S_ISDIR(mount_inode->mode)) {
        close_inode(mount_inode);
        return -EINVAL;
    }
    if (mount_inode->is_mountpoint) {
        close_inode(mount_inode);
        return -EBUSY;
    }

    sigset_t mask = PAUSE_SIGNALS();
    if (check_eintr()) {
        RESTORE_SIGNALS(mask);
        return -EINTR;
    }

    switch (fs) {
        case FS_tmpfs:
        case FS_devfs: // devfs doesn't require a device
        case FS_procfs:
            ret = mount_dev(dev_get_ephemeral(), mount_inode, fs, options);
            close_inode(mount_inode);
            RESTORE_SIGNALS(mask);
            return ret;
        default:
            ret = openat_inode((inode_t*)AT_FDCWD,
                               dev_path,
                               O_RDONLY | ((options & MOUNT_RDONLY) ? 0 : O_WRONLY),
                               0,
                               &dev_inode, 0);
            if (ret < 0 || dev_inode == NULL) {
                if (dev_inode != NULL ) close_inode(dev_inode);
                RESTORE_SIGNALS(mask);
                return ret;
            }
            if (!(S_ISBLK(dev_inode->mode) || S_ISCHR(dev_inode->mode))) {
                close_inode(mount_inode);
                close_inode(dev_inode);
                RESTORE_SIGNALS(mask);
                return -ENODEV;
            }
            ret = mount_dev(dev_inode->device, mount_inode, fs, options);
            close_inode(dev_inode);
            close_inode(mount_inode);
            RESTORE_SIGNALS(mask);
            return ret;
    }
}

long sys_umount(const char * mount_path) {
    if (mount_path == NULL) return -EFAULT;
    if (current_process->euid != 0)
        return -EPERM;

    inode_t * mount_inode = NULL;
    // this could be done without O_PATH (sb->mountpoint), but I think that it's cleaner like this
    int ret = openat_inode((inode_t*)AT_FDCWD, mount_path, O_RDONLY | O_DIRECTORY | O_NOXDEV, 0, &mount_inode, 0);
    if (ret < 0 || mount_inode == NULL) return ret;

    if (!mount_inode->is_mountpoint) {
        close_inode(mount_inode);
        return -EINVAL;
    }
    rw_spinlock_acquire_write(&mount_tree_lock); // prevents openat_inode from creating a new instance

    superblock_t * target = mount_inode->next_superblock;

    if (target->instances > 0) {
        rw_spinlock_release_write(&mount_tree_lock);
        close_inode(mount_inode);
        return -EBUSY;
    }

    mount_inode->is_mountpoint = 0;
    __atomic_sub_fetch(&mount_inode->instances, 1, __ATOMIC_RELEASE);
    close_inode(mount_inode);

    sigset_t mask = PAUSE_SIGNALS();
    int old_sig = current_thread->sa_to_be_handled;
    current_thread->sa_to_be_handled = 0;

    if (target->funcs->fs_deinit)
        target->funcs->fs_deinit(target);
    if (target->fd)
        close_file(target->fd);

    target->is_mounted = 0;

    current_thread->sa_to_be_handled = old_sig;
    RESTORE_SIGNALS(mask);

    rw_spinlock_release_write(&mount_tree_lock);
    return 0;
}


long sys_swaproot(const char * new_root, const char * old_root_mnt) {
    static spinlock_t swap_lock = {0};

    if (!new_root || !old_root_mnt) return -EFAULT;
    if (current_process->euid != 0)
        return -EPERM;

    spinlock_acquire(&swap_lock);

    inode_t * new = NULL;
    // this could be done without O_PATH (sb->mountpoint), but I think that it's cleaner like this
    int ret = openat_inode((inode_t *)AT_FDCWD, new_root, O_RDONLY | O_DIRECTORY | O_NOXDEV, 0, &new, 0);
    if (ret < 0 || new == NULL) {
        spinlock_release(&swap_lock);
        return ret;
    }

    if (!new->is_mountpoint || new->backing_superblock == new->next_superblock) {
        spinlock_release(&swap_lock);
        close_inode(new);
        return -EINVAL;
    }

    inode_t * new_dev;
    ret = openat_inode(new, ".", O_RDONLY | O_DIRECTORY, 0, &new_dev, 1);
    if (ret < 0 || new_dev == NULL) {
        spinlock_release(&swap_lock);
        close_inode(new);
        return -EINVAL;
    }
    if (new_dev->backing_superblock != new->next_superblock) {
        spinlock_release(&swap_lock);
        close_inode(new);
        close_inode(new_dev);
        return -EINVAL;
    }


    inode_t * newold = NULL;
    // this could be done without O_PATH (sb->mountpoint), but I think that it's cleaner like this
    ret = openat_inode((inode_t*)AT_FDCWD, old_root_mnt, O_RDONLY | O_DIRECTORY | O_NOXDEV, 0, &newold, 0);
    if (ret < 0 || newold == NULL) {
        spinlock_release(&swap_lock);
        close_inode(new);
        close_inode(new_dev);
        return ret;
    }
    if (newold->backing_superblock != new_dev->backing_superblock) {
        spinlock_release(&swap_lock);
        close_inode(newold);
        close_inode(new);
        close_inode(new_dev);
        return -EINVAL;
    }
    if (newold->is_mountpoint) {
        mount_race:
        spinlock_release(&swap_lock);
        close_inode(newold);
        close_inode(new);
        close_inode(new_dev);
        return -EBUSY;
    }

    // scawy part >_<
    inode_t * old_root = root_mountpoint->mountpoint;
    rw_spinlock_acquire_write(&mount_tree_lock);
    if (newold->is_mountpoint) {
        rw_spinlock_release_write(&mount_tree_lock);
        goto mount_race;
    }

    // unlink old mount points
    __atomic_store_n(&new->is_mountpoint, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&new->next_superblock, NULL, __ATOMIC_RELEASE);
    __atomic_store_n(&old_root->is_mountpoint, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&old_root->next_superblock, NULL, __ATOMIC_RELEASE);

    // relink new_dev as the new root
    __atomic_store_n(&new_dev->is_mountpoint, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&new_dev->next_superblock, new_dev->backing_superblock, __ATOMIC_RELEASE);
    __atomic_store_n(&new_dev->backing_superblock->mountpoint, new_dev, __ATOMIC_RELEASE);
    __atomic_store_n(&root_mountpoint->mountpoint, new_dev, __ATOMIC_RELEASE);

    // relink old_root to newold
    __atomic_store_n(&newold->is_mountpoint, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&newold->next_superblock, old_root->backing_superblock, __ATOMIC_RELEASE);
    __atomic_store_n(&newold->next_superblock->mountpoint, newold, __ATOMIC_RELEASE);


    spinlock_acquire(&scheduler_lock);
    for (process_t * proc = process_list; proc; proc = proc->next) {
        if (proc->root == old_root) {
            __atomic_sub_fetch(&proc->root->instances, 1, __ATOMIC_RELEASE);
            proc->root = new_dev;
            __atomic_add_fetch(&proc->root->instances, 1, __ATOMIC_ACQUIRE);
        }
    }
    spinlock_release(&scheduler_lock);

    rw_spinlock_release_write(&mount_tree_lock);

    spinlock_release(&swap_lock);
    // we on purpose leak newold and new_dev to achieve the +1 for mount points
    close_inode(new); // our open
    close_inode(new); // previous mountpoint
    close_inode(old_root); // previous mountpoint
    return 0;
}