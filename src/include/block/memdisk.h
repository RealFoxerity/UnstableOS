#ifndef MEMDISK_H
#define MEMDISK_H
#include <stddef.h>
#include "../../include/devs.h"
#include "../../include/mm/kernel_memory.h"

#define ___MEMDISKS_BASE  0x03000000
// this value ultimately decides the size of initramfs, we don't do remapping of multiboot mods and bootloaders like grub place the the mods immediately after the kernel
// at 0x300 0000 that leaves around 49MB - size of the kernel for the module before crashing into memdisks

#define MEMDISKS_BASE ((void*)___MEMDISKS_BASE)
#define DEFAULT_MEMDISK_SIZE 0x007FF000 // ~8.3 MB per memdisk

#define GET_MEMDISK_IDX(ptr) (((unsigned long)(ptr)-0x03000000)/DEFAULT_MEMDISK_SIZE)

#if __MEMDISKS_BASE + MEMDISK_LIMIT_KERNEL*DEFAULT_MEMDISK_SIZE > ___KERNEL_ADDRESS_SPACE_VADDR
#error "Memdisks overlap kernel's PDEs!"
#endif

struct {
    char used;
    unsigned long busy; // amount of readers/writers, if 0, can deinit
    char is_allocated; // 1 = was initialized using memdisk_alloc and so has to be then freed, 0 = created from memory range
    void * start_addr;
    size_t size;
} typedef memdisk_t;

extern memdisk_t memdisks[MEMDISK_LIMIT_KERNEL];

#include "../kernel_spinlock.h"
extern spinlock_t memdisk_lock;


dev_t memdisk_alloc(); // allocates a memdisk of DEFAULT_MEMDISK_SIZE size
dev_t memdisk_from_range(void * vaddr, size_t n); // creates a memdisk from address ranges, no allocations
// returns (dev_t) -1 on error

long memdisk_deinit(dev_t dev); // additionally, if memdisk was allocated, frees all pages

#define ssize_t long
ssize_t memdisk_read_internal(dev_t dev, size_t seek, void * s, size_t n);
ssize_t memdisk_write_internal(dev_t dev, size_t seek, const void * s, size_t n);

#include "../../include/fs/fs.h"
ssize_t memdisk_read(file_descriptor_t * fd, void * s, size_t n);
ssize_t memdisk_write(file_descriptor_t * fd, const void * s, size_t n);
off_t memdisk_seek(file_descriptor_t * fd, off_t off, int whence);

#endif