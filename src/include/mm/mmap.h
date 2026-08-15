#ifndef MMAP_H
#define MMAP_H

#include "fs/fs.h"
#include "rbtree.h"

#include <stddef.h>
#include <sys/types.h>

struct vm_record {
    rbtree_t node; // contains the vaddr as node->ptr
    size_t len;

    file_descriptor_t * backing_fd; // only relevant for non-MAP_ANONYMOUS, NULL otherwise
    off_t mapping_offset;

    int prot;
    int private;
};

struct page_fault_error {
    unsigned long P    : 1;
    unsigned long W    : 1;
    unsigned long U    : 1;
    unsigned long RSVD : 1;
    unsigned long ID   : 1;
    unsigned long PK   : 1;
    unsigned long SS   : 1;
    unsigned long HLAT : 1;
    unsigned long SGX  : 1;
} __attribute__((packed));

// note: MAP_SHARED mmap()ed regions do not follow ref counting at any point
//  and rely on the inode mmap page cache instance counters

// note: all of these functions only work on current_process
char mmap_page_fault(void * fault_addr, struct page_fault_error error); // returns 0 on valid, 1 on sigsegv, -1 on sigbus
void *mmap_file(void * addr, size_t len, int prot, int flags, file_descriptor_t * file, off_t off);
void *sys_mmap(void * addr, size_t len, int prot, int flags, int fd, off_t off);
int sys_mprotect(void * addr, size_t len, int prot);
int sys_munmap(void * addr, size_t len);

// expects external read locking of vm_lock
struct vm_record * mmap_dup_vm(const struct vm_record * vmr);

// for paging_check_address_range
// maps pages in when required
// can't be bothered to do properly
// TODO: rewrite to something more performant later
// 0 = no found or permission mismatch, 1 = found and mapped
// requires external vm_lock read lock
char mmap_check_address(const void * addr, char writable);

// for paging_check_address_range
// marks mmap page caches dirty and remarks page as writable if possible
// 0 = no found or permission mismatch, 1 = found and marked dirty
// requires external vm_lock read lock
char mmap_mark_page_dirty(const void * addr);
#endif