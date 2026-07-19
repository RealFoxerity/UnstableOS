// functions to manage read/write/seek/ioctl operations on dev nodes
// implemented using a hashmap

// NOTE: this relies on heap being initialized, don't try to register a lot of devices beforehand
// one of the registered ones is the first TTY

#include "kernel.h"
#include <UnstableOS/devs.h>
#include "dev_ops.h"
#include "fs/fs.h"
#include "mm/kernel_memory.h"
#include "kernel_spinlock.h"
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

static rw_spinlock_t devs_lock = {0};

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

    rw_spinlock_acquire_write(&devs_lock);
    if (hash_entry->dev_ops == NULL) {
        hash_entry->dev_ops = dev_ops;
        hash_entry->dev = dev;
        rw_spinlock_release_write(&devs_lock);
        return;
    }

    struct dev_operations_node * last = NULL;
    for (; hash_entry != NULL; last = hash_entry, hash_entry = hash_entry->next) {
        if (hash_entry->dev == dev) { // found the entry, current behavior is to overwrite
            hash_entry->dev_ops = dev_ops;
            rw_spinlock_release_write(&devs_lock);
            return;
        }
    }
    last->next = kalloc(sizeof(struct dev_operations_node));
    kassert(last->next);
    last->next->dev = dev;
    last->next->dev_ops = dev_ops;
    last->next->next = NULL;
    rw_spinlock_release_write(&devs_lock);
}

// special value to throw ENODEV on
static struct dev_operations stub = {.seek = (void*)1};
// passing by value to avoid potential UAF on ->dev_ops
// dev_ops should be static and not heap allocated, but to be sure
struct dev_operations dev_ops_lookup(dev_t dev) {
    rw_spinlock_acquire_read(&devs_lock);
    for (
        const struct dev_operations_node * hash_entry = &dev_hashmap[dev_get_key(dev)];
        hash_entry != NULL;
        hash_entry = hash_entry->next
    ) {
        if (hash_entry->dev == dev) {
            rw_spinlock_release_read(&devs_lock);
            return *hash_entry->dev_ops;
        }
    }
    rw_spinlock_release_read(&devs_lock);
    return stub;
}

void dev_ops_remove(dev_t dev) {
    rw_spinlock_acquire_write(&devs_lock);
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
            rw_spinlock_release_write(&devs_lock);
            return;
        }
    }
    rw_spinlock_release_write(&devs_lock);
}


ssize_t pread_dev(file_descriptor_t *file, void *buf, size_t count, off_t offset) {
    if (offset < 0) return -EINVAL;

    kassert(file);
    kassert(file->inode);
    kassert(S_ISCHR(file->inode->mode) || S_ISBLK(file->inode->mode));
    kassert(buf);

    struct dev_operations dev_ops = dev_ops_lookup(file->inode->device);
    if (dev_ops.seek == (void*)1) return -ENXIO;
    if (!file->inode->dev_opened) return -EIO;

    if (dev_ops.pread == NULL) return -EINVAL;
    if (count == 0) return 0;


    return dev_ops.pread(file, buf, count, offset);
}
ssize_t pwrite_dev(file_descriptor_t *file, const void *buf, size_t count, off_t offset) {
    if (offset < 0) return -EINVAL;

    kassert(file);
    kassert(file->inode);
    kassert(S_ISCHR(file->inode->mode) || S_ISBLK(file->inode->mode));
    kassert(buf);

    struct dev_operations dev_ops = dev_ops_lookup(file->inode->device);
    if (dev_ops.seek == (void*)1) return -ENXIO;
    if (!file->inode->dev_opened) return -EIO;

    if (dev_ops.pwrite == NULL) return -EINVAL;
    if (count == 0) return 0;

    return dev_ops.pwrite(file, buf, count, offset);
}
off_t seek_dev(file_descriptor_t * file, off_t offset, int whence) {
    kassert(file);
    kassert(file->inode);
    kassert(S_ISCHR(file->inode->mode) || S_ISBLK(file->inode->mode));

    struct dev_operations dev_ops = dev_ops_lookup(file->inode->device);
    if (dev_ops.seek == (void*)1) return -ENXIO;
    if (!file->inode->dev_opened) return -EIO;

    if (dev_ops.seek == NULL) return -ESPIPE;

    return dev_ops.seek(file, offset, whence);
}
long ioctl_dev(file_descriptor_t *file, unsigned long request, void * arg) {
    kassert(file);
    kassert(file->inode);

    // following the linux-like approach that everything can have an IOCTL
    // for context: POSIX normally mandates ENOTTY on non-STREAMS devices,
    // and EINVAL on invalid request and/or arg
    // meanwhile linux just does ENOTTY whenever the request doesn't match the device
    if (!(
#ifndef POSIX_LIKE_IOCTL_ERRORS
        S_ISBLK(file->inode->mode) ||
#endif
        S_ISCHR(file->inode->mode)))
            return -ENOTTY;

    if (MAJOR(file->inode->device) != __IOCTL_DEV(request))
#ifndef POSIX_LIKE_IOCTL_ERRORS
        return -ENOTTY;
#else
        return -EINVAL;
#endif

    struct dev_operations dev_ops = dev_ops_lookup(file->inode->device);
    if (dev_ops.seek == (void*)1) return -ENXIO;
    if (!file->inode->dev_opened) return -EIO;

    if (dev_ops.ioctl == NULL) return -ENOTTY;

    return dev_ops.ioctl(file, request, arg);
}


// we can either properly return -ENXIO
// or just pretend everything's okay
// we need inode opens to succeed to do stuff like
// stat(), chmod(), unlink()...
// so we delay the ENXIO until read/write/seek/ioctl
long open_dev(inode_t * inode) {
    kassert(inode);
    kassert(S_ISCHR(inode->mode) || S_ISBLK(inode->mode));

    if (MAJOR(inode->device) == DEV_MAJ_EPHEMERAL) return 0; // these devices are placeholders anyway

    struct dev_operations dev_ops = dev_ops_lookup(inode->device);
    //if (dev_ops.seek == (void*)1) return -ENXIO;
    if (dev_ops.seek == (void*)1) return 0;

    if (dev_ops.open == NULL) {
        inode->io_block_size = 512; // good default

        // so that io_block_size assignment doesn't float beneath this
        __atomic_store_n(&inode->dev_opened, 1, __ATOMIC_RELEASE);
        return 0;
    }

    if (inode->dev_opened) return 0;

    return dev_ops.open(inode);
}

long close_dev(inode_t * inode) {
    kassert(inode);
    kassert(S_ISCHR(inode->mode) || S_ISBLK(inode->mode));

    if (MAJOR(inode->device) == DEV_MAJ_EPHEMERAL) return 0;

    struct dev_operations dev_ops = dev_ops_lookup(inode->device);
    if (dev_ops.seek == (void*)1) return -ENODEV;

    if (dev_ops.close == NULL) {
        inode->dev_opened = 0;
        return 0;
    }

    if (!inode->dev_opened) return 0;

    return dev_ops.close(inode);
}

extern void framebuffer_register();
extern void dev_register_basic_devices();
void dev_initialize_static_devices() {
    framebuffer_register();
    dev_register_basic_devices();
}

dev_t dev_get_ephemeral() {
    static unsigned short last_id = 0;
    return GET_DEV(DEV_MAJ_EPHEMERAL, __atomic_fetch_add(&last_id, 1, __ATOMIC_RELAXED) % 1024);
}



char * dev2string(dev_t device, char * buf_out) {
    if (buf_out == NULL) return NULL;

    switch (MAJOR(device)) {
        case DEV_MAJ_MEM:
            sprintf(buf_out, "mem%d", MINOR(device));
            break;
        case DEV_MAJ_BLOCK0:
        case DEV_MAJ_BLOCK1:
        case DEV_MAJ_BLOCK2:
        case DEV_MAJ_BLOCK3:
            if (MINOR(device)%DRIVE_PART_LIMIT == 0)
                sprintf(buf_out, "hd%d",
                    (MAJOR(device) - DEV_MAJ_BLOCK0) * (1024/DRIVE_PART_LIMIT) + MINOR(device) / DRIVE_PART_LIMIT);
            else
                sprintf(buf_out, "hd%dp%d",
                    (MAJOR(device) - DEV_MAJ_BLOCK0) * (1024/DRIVE_PART_LIMIT) + MINOR(device) / DRIVE_PART_LIMIT,
                    MINOR(device) % DRIVE_PART_LIMIT);
            break;
        case DEV_MAJ_EPHEMERAL:
            sprintf(buf_out, "?eph%d", MINOR(device));
            break;
        case DEV_MAJ_TTY:
            switch (MINOR(device)) {
                case 0 ... __TTY_CONSOLE - 1:
                    sprintf(buf_out, "tty%d", MINOR(device)+1);
                    break;
                case __TTY_CONSOLE ... __TTY_CONSOLE + __TTY_SERIAL - 1:
                    sprintf(buf_out, "ttyS%d", MINOR(device)+1);
                    break;
                case __TTY_CONSOLE + __TTY_SERIAL ... DEV_TTY_CURRENT - 1:
                    sprintf(buf_out, "pty%d", MINOR(device)+1);
                    break;
                case DEV_TTY_CURRENT:
                    strcpy(buf_out, "tty");
                    break;
                case DEV_TTY_CONSOLE:
                    strcpy(buf_out, "console");
                    break;
                default:
                    sprintf(buf_out, "?tty%d", MINOR(device));
                    break;
            }
            break;
        case DEV_MAJ_FB:
            sprintf(buf_out, "fb%d", MINOR(device));
            break;
        case DEV_MAJ_MISC:
            switch (MINOR(device)) {
                case DEV_MISC_PS2MOUSE:
                    strcpy(buf_out, "psaux");
                    break;
                case DEV_MISC_ZERO:
                    strcpy(buf_out, "zero");
                    break;
                case DEV_MISC_NULL:
                    strcpy(buf_out, "null");
                    break;
                case DEV_MISC_RANDOM:
                    strcpy(buf_out, "random");
                    break;
                default:
                    sprintf(buf_out, "?misc%d", MINOR(device));
                    break;
            }
        default:
            sprintf(buf_out, "?maj%dmin%d\n", MAJOR(device), MINOR(device));
            break;
    }

    return buf_out;
}