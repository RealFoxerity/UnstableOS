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

inode_t * get_inode(dev_t device, size_t inode_number) { // gets the inode structure with a given device id and fs specific inode id
    kassert(kernel_inodes);
    spinlock_acquire(&kernel_inode_lock);

    inode_t * inode = NULL;
    for (int i = 0; i < INODE_LIMIT_KERNEL; i++) {
        if (kernel_inodes[i] != NULL && kernel_inodes[i]->instances != 0 && 
                kernel_inodes[i]->device == device && kernel_inodes[i]->id == inode_number) {
            
            inode = kernel_inodes[i];
            break;
        }
    }
    if (inode == NULL) panic("No valid inode with specified device number and id\n");

    spinlock_release(&kernel_inode_lock);
    return inode;
}

inode_t * inode_from_path(const char * path) {
    spinlock_acquire(&kernel_inode_lock);



    spinlock_release(&kernel_inode_lock);
}
