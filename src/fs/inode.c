#include "../include/fs/fs.h"
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

static inode_t * __get_inode_raw_device(dev_t device) {
    kassert(kernel_inodes);

    inode_t * inode = NULL;
    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        if (kernel_inodes[i] != NULL && kernel_inodes[i]->instances != 0 && 
                kernel_inodes[i]->device == device && kernel_inodes[i]->is_raw_device) {
            
            inode = kernel_inodes[i];
            break;
        }
    }
    //if (inode == NULL) panic("No valid raw device inode with specified device number\n");

    return inode;
}

inode_t * get_inode_raw_device(dev_t device) { // gets the inode representing a raw device instead of a file
    spinlock_acquire(&kernel_inode_lock);
    
    inode_t * inode =  __get_inode_raw_device(device);

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
inode_t * create_inode(superblock_t * sb, void * inode_number) {
    spinlock_acquire(&kernel_inode_lock);
    inode_t * inode = __get_inode(sb, inode_number);
    if (inode != NULL) {
        __atomic_add_fetch(&inode->instances, 1, __ATOMIC_RELAXED);
        goto ret;
    }

    inode = get_free_inode();
    kassert(inode);

    inode->backing_superblock = sb;
    inode->id = inode_number;

    ret:
    spinlock_release(&kernel_inode_lock);
    return inode;
}

void close_inode(inode_t *inode) {
    __atomic_sub_fetch(&inode->instances, 1, __ATOMIC_RELAXED);
}

inode_t * inode_from_device(dev_t device) {
    spinlock_acquire(&kernel_inode_lock);

    inode_t * new_inode = __get_inode_raw_device(device);
    if (new_inode) {
        new_inode->instances ++;
        goto end;
    }
    
    new_inode = get_free_inode();

    new_inode->is_raw_device = 1;
    new_inode->device = device;

    end:
    spinlock_release(&kernel_inode_lock);
    return new_inode;
}

void inode_change_mode(inode_t * inode, unsigned char new_mode) {
    kassert(inode);
    spinlock_acquire(&inode->lock);
    inode->mode = new_mode;
    spinlock_release(&inode->lock);
}


