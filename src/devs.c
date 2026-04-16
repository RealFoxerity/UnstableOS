// functions to manage read/write/seek/ioctl operations on dev nodes
// implemented using a hashmap

// NOTE: this relies on heap being initialized, don't try to register a lot of devices beforehand
// one of the registered ones is the first TTY

#include "kernel.h"
#include "devs.h"
#include "dev_ops.h"
#include "fs/fs.h"
#include "mm/kernel_memory.h"
#include "kernel_spinlock.h"
#include <string.h>
#include <errno.h>

static spinlock_t devs_lock = {0};

#define DEVS_BUCKETS 32

struct dev_operations_node {
    dev_t dev;
    const struct dev_operations *dev_ops;
    struct dev_operations_node *next;
};

static struct dev_operations_node dev_hashmap[DEVS_BUCKETS] = {0};

static unsigned int dev_get_key(dev_t dev) {
    return dev * 1103515245 % DEVS_BUCKETS;
}

// if one already exists, it gets overwritten!
void dev_register_ops(dev_t dev, const struct dev_operations * dev_ops) {
    kassert(dev_ops);
    struct dev_operations_node * hash_entry = &dev_hashmap[dev_get_key(dev)];

    spinlock_acquire(&devs_lock);
    if (hash_entry->dev_ops == NULL) {
        hash_entry->dev_ops = dev_ops;
        hash_entry->dev = dev;
        spinlock_release(&devs_lock);
        return;
    }

    struct dev_operations_node * last = NULL;
    for (; hash_entry != NULL; last = hash_entry, hash_entry = hash_entry->next) {
        if (hash_entry->dev == dev) { // found the entry, current behavior is to overwrite
            hash_entry->dev_ops = dev_ops;
            spinlock_release(&devs_lock);
            return;
        }
    }
    last->next = kalloc(sizeof(struct dev_operations_node));
    kassert(last->next);
    last->next->dev = dev;
    last->next->dev_ops = dev_ops;
    last->next->next = NULL;
    spinlock_release(&devs_lock);
}

// full of NULL pointers so that every operation fails with EINVAL/EISPIPE
static struct dev_operations stub = {0};
// passing by value to avoid potential UAF on ->dev_ops
// dev_ops should be static and not heap allocated, but to be sure
struct dev_operations dev_ops_lookup(dev_t dev) {
    spinlock_acquire(&devs_lock);
    for (
        const struct dev_operations_node * hash_entry = &dev_hashmap[dev_get_key(dev)];
        hash_entry != NULL;
        hash_entry = hash_entry->next
    ) {
        if (hash_entry->dev == dev) {
            spinlock_release(&devs_lock);
            return *hash_entry->dev_ops;
        }
    }
    spinlock_release(&devs_lock);
    return stub;
}

void dev_ops_remove(dev_t dev) {
    spinlock_acquire(&devs_lock);
    struct dev_operations_node * last = NULL;
    for (
        struct dev_operations_node *hash_entry = &dev_hashmap[dev_get_key(dev)];
        hash_entry != NULL;
        last = hash_entry, hash_entry = hash_entry->next
    ) {
        if (hash_entry->dev == dev) {
            if (last != NULL) {
                last->next = hash_entry->next;
                kfree(hash_entry);
            } else {
                // hash_entry here being the dev_hashmap element itself
                if (hash_entry->next == NULL) {
                    memset(hash_entry, 0, sizeof(struct dev_operations_node));
                } else {
                    struct dev_operations_node * old = hash_entry->next;
                    memcpy(hash_entry, hash_entry->next, sizeof(struct dev_operations_node));
                    kfree(old);
                }
            }
            spinlock_release(&devs_lock);
            return;
        }
    }
    spinlock_release(&devs_lock);
}


ssize_t read_dev(file_descriptor_t *file, void *buf, size_t count) {
    kassert(file);
    kassert(file->inode);
    kassert(S_ISCHR(file->inode->mode) || S_ISBLK(file->inode->mode));
    kassert(buf);

    struct dev_operations dev_ops = dev_ops_lookup(file->inode->device);
    if (dev_ops.read == NULL) return -EINVAL;
    if (count == 0) return 0;

    return dev_ops.read(file, buf, count);
}
ssize_t write_dev(file_descriptor_t *file, const void *buf, size_t count) {
    kassert(file);
    kassert(file->inode);
    kassert(S_ISCHR(file->inode->mode) || S_ISBLK(file->inode->mode));
    kassert(buf);

    struct dev_operations dev_ops = dev_ops_lookup(file->inode->device);
    if (dev_ops.write == NULL) return -EINVAL;
    if (count == 0) return 0;

    return dev_ops.write(file, buf, count);
}
off_t seek_dev(file_descriptor_t * file, off_t offset, int whence) {
    kassert(file);
    kassert(file->inode);
    kassert(S_ISCHR(file->inode->mode) || S_ISBLK(file->inode->mode));

    struct dev_operations dev_ops = dev_ops_lookup(file->inode->device);
    if (dev_ops.seek == NULL) return -ESPIPE;

    return dev_ops.seek(file, offset, whence);
}
long ioctl_dev(file_descriptor_t *file, unsigned long cmd, void * arg) {
    kassert(file);
    kassert(file->inode);
    kassert(S_ISCHR(file->inode->mode) || S_ISBLK(file->inode->mode));

    struct dev_operations dev_ops = dev_ops_lookup(file->inode->device);
    if (dev_ops.ioctl == NULL) return -EINVAL;

    return dev_ops.ioctl(file, cmd, arg);
}


extern void framebuffer_register();
extern void dev_register_basic_devices();
void dev_initialize_static_devices() {
    framebuffer_register();
    dev_register_basic_devices();
}