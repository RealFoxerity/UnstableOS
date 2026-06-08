#include "dev_ops.h"
#include "errno.h"
#include "../include/fs/fs.h"
#include "../include/fs/vfs.h"
#include "../include/mm/kernel_memory.h"
#include "../include/kernel.h"
#include "../../libc/src/include/string.h"

inode_t ** kernel_inodes;

spinlock_t kernel_inode_lock = {0};

void init_inodes() {
    kernel_inodes = kalloc(sizeof(inode_t *) * INODE_LIMIT_KERNEL);
    kassert(kernel_inodes);
    memset(kernel_inodes, 0, sizeof(inode_t *) * INODE_LIMIT_KERNEL);
}

static void cleanup_inode_list() { // acquire lock before this
    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        if (kernel_inodes[i] != NULL && kernel_inodes[i]->instances == 0) {
            kfree(kernel_inodes[i]);
        }
    }
}

// acquire lock before this, sets the inode instance count to 1
inode_t * get_free_inode() {
    kassert(kernel_inodes);
    //cleanup_inode_list();
    // considering a single inode is 40 bytes, and thus the entire list is 328K, i don't think we need the cleanup

    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        if (kernel_inodes[i] == NULL) {
            kernel_inodes[i] = kalloc(sizeof(inode_t));
            if (kernel_inodes[i] == NULL) panic("Not enough memory to allocate new inode");
            memset(kernel_inodes[i], 0, sizeof(inode_t));
            kernel_inodes[i]->instances = 1;
            return kernel_inodes[i];
        }
        else if (kernel_inodes[i]->instances == 0) {
            memset(kernel_inodes[i], 0, sizeof(inode_t));
            kernel_inodes[i]->instances = 1;
            return kernel_inodes[i];
        }
    }
    panic("No free inodes available!");
    //return NULL;
}

inode_t * __get_inode_raw_device(dev_t device) {
    kassert(kernel_inodes);

    inode_t * inode = NULL;
    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        if (kernel_inodes[i] != NULL && kernel_inodes[i]->instances != 0 &&
                (S_ISBLK(kernel_inodes[i]->mode) || S_ISCHR(kernel_inodes[i]->mode)) &&
                kernel_inodes[i]->device == device) {

            inode = kernel_inodes[i];
            break;
        }
    }
    //if (inode == NULL) panic("No valid raw device inode with specified device number\n");

    return inode;
}

inode_t * get_inode_raw_device(dev_t device) { // gets the inode representing a raw device instead of a file
    spinlock_acquire(&kernel_inode_lock);

    inode_t * inode = __get_inode_raw_device(device);

    spinlock_release(&kernel_inode_lock);
    return inode;
}

inode_t * __get_inode(superblock_t * sb, void * inode_number) {
    kassert(kernel_inodes);

    inode_t * inode = NULL;
    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        if (kernel_inodes[i] != NULL && kernel_inodes[i]->instances != 0 &&
                kernel_inodes[i]->backing_superblock == sb && kernel_inodes[i]->id == inode_number) {

            inode = kernel_inodes[i];
            break;
        }
    }
    //if (inode == NULL) panic("No valid inode with specified superblock and id\n");

    return inode;
}

inode_t * get_inode(superblock_t * sb, void * inode_number) {
    spinlock_acquire(&kernel_inode_lock);
    inode_t * inode = __get_inode(sb, inode_number);
    spinlock_release(&kernel_inode_lock);
    return inode;
}

long register_inode(const inode_t * inode, inode_t ** inode_out) {
    if (inode == NULL) return -EFAULT;

    long status = 0;
    spinlock_acquire(&kernel_inode_lock);
    inode_t * new_inode = __get_inode(inode->backing_superblock, inode->id);
    if (new_inode != NULL) {
        __atomic_add_fetch(&new_inode->instances, 1, __ATOMIC_RELAXED);
        goto ret;
    }

    new_inode = get_free_inode();
    if (new_inode == NULL) {
        status = -ENOMEM;
        goto ret;
    }

    // can't do a memcpy in case we found the old one, and it's mounted, or has a different instance count...
    new_inode->id = inode->id;
    new_inode->mode = inode->mode;
    new_inode->size = inode->size;
    new_inode->backing_superblock = inode->backing_superblock;
    if (S_ISCHR(inode->mode) || S_ISBLK(inode->mode))
        new_inode->device = inode->device;
    else if S_ISFIFO(inode->mode)
        new_inode->pipe   = inode->pipe;

    if (S_ISCHR(inode->mode) || S_ISBLK(inode->mode))
        if ((status = open_dev(new_inode)) < 0) {
            new_inode->instances = 0; // "free" the inode
            new_inode = NULL;
        }

    ret:
    *inode_out = new_inode;
    spinlock_release(&kernel_inode_lock);
    return status;
}

void close_inode(inode_t *inode) {
    spinlock_acquire(&inode->lock);
    spinlock_acquire(&kernel_inode_lock);
    if (__atomic_sub_fetch(&inode->instances, 1, __ATOMIC_RELAXED) == 0) {
        if (inode->backing_superblock &&
            inode->backing_superblock->funcs &&
            inode->backing_superblock->funcs->release)
            inode->backing_superblock->funcs->release(inode);
        if (S_ISFIFO(inode->mode)) kfree(inode->pipe);
        if (S_ISCHR(inode->mode) || S_ISBLK(inode->mode))
            close_dev(inode);
    }
    spinlock_release(&inode->lock);
    spinlock_release(&kernel_inode_lock);
}

long inode_from_device(dev_t device, inode_t ** inode_out) {
    if (inode_out == NULL) return -EFAULT;
    static unsigned long long ephemeral_id = 0;

    inode_t new = {
        .id = (void*)__atomic_fetch_add(&ephemeral_id, 1, __ATOMIC_RELAXED),
        .mode = DEV_IS_CHAR(device) ? S_IFCHR : S_IFBLK,
        .device = device,
    };

    inode_t * ret = NULL;
    long status = register_inode(&new, &ret);
    *inode_out = ret;

    return status;
}

void inode_change_mode(inode_t * inode, unsigned short new_mode) {
    kassert(inode);
    __atomic_store_n(&inode->mode, new_mode, __ATOMIC_RELAXED);
}


