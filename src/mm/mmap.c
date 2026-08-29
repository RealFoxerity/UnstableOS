#include "mm/kernel_memory.h"
#include "kernel.h"
#include "kernel_spinlock.h"
#include "fs/fs.h"
#include "rbtree.h"

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

#include "dev_ops.h"

// TODO: Implement non-MAP_FIXED
// TODO: Implement a page tracker for shared anonymous mappings, see todo in mmap_file

// returns 0 on valid, 1 on sigsegv, -1 on sigbus
char mmap_page_fault(void * fault_addr, struct page_fault_error error) {
    rw_spinlock_acquire_read(&current_process->vm_lock);
    struct vm_record * closest = (struct vm_record *)rbtree_search_lte((rbtree_t*)current_process->vm, (uintptr_t)fault_addr);

    // the align down here because we want to allow this for anon mappings and throw SIGBUS on file mappings
    if (!closest || closest->node.val + closest->len < ((uintptr_t)fault_addr & ~(PAGE_SIZE-1)) ||
        (error.W && !(closest->prot & PROT_WRITE)) ||
        (!error.W && !(closest->prot & PROT_READ)))
    {
        rw_spinlock_release_read(&current_process->vm_lock);
        return 1;
    }

    int mapping_flags = 0;
    if (closest->prot)
        mapping_flags |= PTE_PDE_PAGE_USER_ACCESS;
    if (closest->prot & PROT_WRITE)
        mapping_flags |= PTE_PDE_PAGE_WRITABLE;
    off_t target_offset = ((uintptr_t)fault_addr & ~(PAGE_SIZE - 1)) - closest->node.val + closest->mapping_offset;

    if (error.P || paging_get_pte(fault_addr)) { // either bug, dirty marker, or we raced on a different page fault
        if (error.W &&
            closest->prot & PROT_WRITE &&
            !closest->private &&
            closest->backing_fd &&
            S_ISREG(closest->backing_fd->inode->instances)
        ) { // mark the mmap cache dirty
            rw_spinlock_acquire_write(&closest->backing_fd->inode->mmap_pc_lock);
            struct mmap_page_cache * mpc =
                (void*)rbtree_search_exact(
                    (rbtree_t *)closest->backing_fd->inode->mmap_page_cache,
                    target_offset >> 12
            );
            if (mpc) {
                __atomic_store_n(&mpc->dirty, 1, __ATOMIC_RELEASE);
            } // else { howwwww??????? }
            rw_spinlock_release_write(&closest->backing_fd->inode->mmap_pc_lock);
        }
        paging_change_flags(fault_addr, 1, mapping_flags);
        rw_spinlock_release_read(&current_process->vm_lock);
        return 0;
    }

    if (!closest->backing_fd) { // anonymous mapping
        if (!paging_add_page(fault_addr, mapping_flags)) {
            rw_spinlock_release_read(&current_process->vm_lock);
            return 1;
        }
        rw_spinlock_release_read(&current_process->vm_lock);
        return 0;
    }
    kassert(closest->backing_fd);

    if (!S_ISREG(closest->backing_fd->inode->mode))
        return -1;
        //panic("Missing pages for mmaped device");

    if ((off_t)(uintptr_t)fault_addr - closest->node.val + closest->mapping_offset > closest->backing_fd->inode->size) {
        rw_spinlock_release_read(&current_process->vm_lock);
        return -1;
    }

    char ret = 1;


    disable_wp(); // we plan on changing potentially unwritable sections

    if (!closest->private) {
        rw_spinlock_acquire_write(&closest->backing_fd->inode->mmap_pc_lock);
        struct mmap_page_cache * mpc =
            (void*)rbtree_search_exact(
                (rbtree_t *)closest->backing_fd->inode->mmap_page_cache,
                target_offset >> 12
        );
        if (mpc) {
            __atomic_add_fetch(&mpc->instances, 1, __ATOMIC_ACQUIRE);
            if (error.W)
                __atomic_store_n(&mpc->dirty, 1, __ATOMIC_RELEASE);
            else
                mapping_flags &= ~PTE_PDE_PAGE_WRITABLE; // for flagging dirty by page fault
            paging_map_phys_addr(mpc->page, fault_addr, mapping_flags);
            ret = 0;
            goto fin;
        }
    }

    if (!paging_add_page(fault_addr, mapping_flags))
        goto fin;

    size_t to_read = 4096;
    if (((uintptr_t)fault_addr & ~(PAGE_SIZE - 1)) - closest->node.val + 4096 > closest->len && closest->private)
        to_read = closest->len % PAGE_SIZE;

    // so that pread doesn't throw EINTR
    sigset_t old_sigs = PAUSE_SIGNALS();
    if (pread_file(closest->backing_fd,
            (void*)((uintptr_t)fault_addr & ~(PAGE_SIZE - 1)), to_read,
                target_offset)
        < 0) {
        RESTORE_SIGNALS(old_sigs);
        ret = -1;
        goto fin;
    }
    RESTORE_SIGNALS(old_sigs);

    if (!closest->private) {
        struct mmap_page_cache * new_entry = kalloc(sizeof(struct mmap_page_cache));
        if (!new_entry)
            goto fin;

        new_entry->node.val = target_offset >> 12;
        new_entry->instances = 1;
        new_entry->dirty = (char)error.W;

        fault_addr = (void *)((uintptr_t)fault_addr & ~(PAGE_SIZE-1));
        new_entry->page = paging_virt_addr_to_phys(fault_addr);

        rbtree_add((rbtree_t **)&closest->backing_fd->inode->mmap_page_cache,
                    (rbtree_t*)new_entry);
    }

    ret = 0;

    fin:
    enable_wp();
    if (!closest->private)
        rw_spinlock_release_write(&closest->backing_fd->inode->mmap_pc_lock);
    rw_spinlock_release_read(&current_process->vm_lock);
    return ret;
}

// returns the pointer to the new "right" vmr
// lock write vm_lock externally
static struct vm_record * mmap_split_record(struct vm_record ** vm, struct vm_record * vmr, size_t offset) {
    if (!vmr || !vm)
        return NULL;
    kassert(offset > 0 && !(offset % PAGE_SIZE));
    kassert(offset < vmr->len);
    struct vm_record * new = kalloc(sizeof(struct vm_record));
    kassert(new);
    memcpy(new, vmr, sizeof(struct vm_record));

    vmr->len = offset;

    new->node.ptr += offset;
    new->len -= offset;
    new->mapping_offset += offset;

    rbtree_add((rbtree_t**)vm, (rbtree_t*)new);

    if (new->backing_fd) {
        __atomic_add_fetch(&new->backing_fd->instances, 1, __ATOMIC_ACQUIRE);
        __atomic_add_fetch(&new->backing_fd->inode->mmaped_instances, 1, __ATOMIC_ACQUIRE);
    }

    return new;
}

int munmap_to_vmr(struct vm_record ** vmr_tree, void *addr, size_t len, char ignore_missing, char was_empty);

void *mmap_to_vmr(struct vm_record ** vmr, void *addr, size_t len, int prot, int flags, file_descriptor_t * file, off_t off) {
    if (len == 0 || off < 0 || !vmr)
        return (void*)-EINVAL;
    if (!(flags & MAP_ANONYMOUS) && !file)
        return (void*)-EBADF;

    if (!(flags & MAP_FIXED)) {
        kprintf("stub: mmap() without MAP_FIXED not yet supported\n");
        return (void*)-ENOTSUP;
    }
    if ((uintptr_t)addr + len < (uintptr_t)addr ||
        addr + len > (void *) PROGRAM_PCB_VADDR || // pcb, tls, heap, stack, framebuffer, mmio
        addr < (void*)0x08000000 // GCC's entry + end of our kernel structures
    ) {
        return (void*)-ENOMEM;
    }
    if ((uintptr_t)addr % PAGE_SIZE || (off % PAGE_SIZE & flags & MAP_SHARED)) {
        return (void*)-EINVAL;
    }

    if (flags & MAP_SHARED && flags & MAP_PRIVATE)
        return (void*)-EINVAL;
    if (!(flags & MAP_SHARED) && !(flags & MAP_PRIVATE))
        return (void*)-EINVAL;

    if (!(flags & MAP_ANONYMOUS)) {
        kassert(file->inode);
        if (S_ISCHR(file->inode->mode) ||
            S_ISBLK(file->inode->mode)
        ) {
            if (dev_ops_lookup(file->inode->device).mmap == NULL)
                return (void*)-ENODEV;

            // I refuse to support private mappings of devices
            if (flags & MAP_PRIVATE)
                return (void*)-ENOTSUP;
        } else if (!S_ISREG(file->inode->mode))
            return (void*)-ENODEV;

        if (!(file->flags & O_RDONLY))
            return (void*)-EACCES;
        if (!(file->flags & O_WRONLY) && prot & PROT_WRITE && flags & MAP_SHARED)
            return (void*)-EACCES;
        // our mmap page caches can only hold 44 bits of addressable memory
        // (32 bit rbtree key with 12 bits of granularity)
        if (flags & MAP_SHARED && off + len >= (unsigned long long)1<<44)
            return (void*)-ENXIO;
    } else {
        file = NULL; // to be sure
        if (pf_get_free_memory() < len)
            return (void*)-ENOMEM;
    }

    struct vm_record * new_rec = kalloc(sizeof(struct vm_record));
    if (!new_rec)
        return (void*)-ENOMEM;

    struct vm_record * prev = (struct vm_record *)rbtree_search_lte((rbtree_t *)*vmr, (uintptr_t)addr);
    if (prev && prev->node.ptr + prev->len > addr) {
        kfree(new_rec);
        return (void*)-ENOMEM;
    }
    // MAP_PRIVATE doesn't get shared between forked processes, so relying on CoW is enough
    // TODO: introduce an rbtree page cache per anonymous mapping
    // MAP_SHARED with MAP_ANON was introduced in linux 2.4, maybe there's not many use cases for it?
    if (flags & MAP_ANONYMOUS && flags & MAP_SHARED) {
        int mapping_flags = 0;
        if (prot)
            mapping_flags |= PTE_PDE_PAGE_USER_ACCESS;
        if (prot & PROT_WRITE)
            mapping_flags |= PTE_PDE_PAGE_WRITABLE;
        // may fail, at which point SIGBUS sending in the mmap page fault handler should take over
        // should be a better error than returning ENOMEM here and then unmapping
        paging_map(addr, len, mapping_flags);
    }

    if (!(flags & MAP_ANONYMOUS) && (
        S_ISCHR(file->inode->mode) ||
        S_ISBLK(file->inode->mode)))
    {
        int ret = mmap_dev(file->inode, prot, off, addr, len);
        if (ret < 0) {
            kfree(new_rec);
            return (void*)ret;
        }
    }

    if (!(flags & MAP_ANONYMOUS)) {
        kassert(file && file->inode);
        __atomic_add_fetch(&file->instances, 1, __ATOMIC_ACQUIRE);
        __atomic_add_fetch(&file->inode->mmaped_instances, 1, __ATOMIC_ACQUIRE);
    }

    *new_rec = (struct vm_record) {
        .backing_fd = file,
        .len = len,
        .prot = prot & (PROT_NONE | PROT_READ | PROT_WRITE | PROT_EXEC),
        .mapping_offset = off,
        .private = (flags & MAP_PRIVATE) != 0,
        .node.ptr = addr
    };

    rbtree_add((rbtree_t**)vmr, (rbtree_t *)new_rec);


    return addr;
}
void *mmap_file(void *addr, size_t len, int prot, int flags, file_descriptor_t * file, off_t off) {
    rw_spinlock_acquire_write(&current_process->vm_lock);
    if (flags & MAP_FIXED)
        munmap_to_vmr(&current_process->vm, addr, len, 1, 0);
    void * ret = mmap_to_vmr(&current_process->vm, addr, len, prot, flags, file, off);
    rw_spinlock_release_write(&current_process->vm_lock);
    return ret;
}

void *sys_mmap(void * addr, size_t len, int prot, int flags, int fd, off_t off) {
    if (!(flags & MAP_ANONYMOUS) && (fd < 0 || fd >= FD_LIMIT_PROCESS)) return (void*)-EBADF;

    file_descriptor_t * file = NULL;

    if (!(flags & MAP_ANONYMOUS)) {
        spinlock_acquire(&current_process->lock);
        file = current_process->fds[fd];
        if (file != NULL) {
            __atomic_add_fetch(&file->instances, 1, __ATOMIC_ACQUIRE);
        }
        spinlock_release(&current_process->lock);
        if (!file)
            return (void*)-EBADF;
    }

    void * ret = mmap_file(addr, len, prot, flags, file, off);
    if (!(flags & MAP_ANONYMOUS))
        close_file(file);
    return ret;
}

int sys_mprotect(void * addr, size_t len, int prot) {
    if ((uintptr_t)addr % PAGE_SIZE)
        return -EINVAL;
    if (len == 0)
        return 0; // ig?

    len = (len + PAGE_SIZE - 1)/PAGE_SIZE;

    prot &= PROT_READ | PROT_WRITE | PROT_EXEC;

    int mapping_flags = 0;
    if (prot)
        mapping_flags |= PTE_PDE_PAGE_USER_ACCESS;
    if (prot & PROT_WRITE)
        mapping_flags |= PTE_PDE_PAGE_WRITABLE;

    rw_spinlock_acquire_write(&current_process->vm_lock);
    for (size_t i = 0; i < len;) {
        struct vm_record * vmr =
            (struct vm_record *)rbtree_search_lte((rbtree_t*)current_process->vm, (uintptr_t)addr + i*PAGE_SIZE);

        if (!vmr || vmr->node.ptr + vmr->len < addr) {
            rw_spinlock_release_write(&current_process->vm_lock);
            return -ENOMEM;
        }

        if (vmr->backing_fd && !vmr->private) {
            if (!(vmr->backing_fd->flags & O_WRONLY) && prot & PROT_WRITE) {
                rw_spinlock_release_write(&current_process->vm_lock);
                return -EACCES;
            }
        }

        size_t offset = (uintptr_t)addr + i*PAGE_SIZE - vmr->node.val;

        if (offset != 0 && vmr->prot != prot) {
            vmr = mmap_split_record(&current_process->vm, vmr, offset);
            kassert(vmr);
        }

        size_t pages_to_change = 0;
        if ((vmr->len + PAGE_SIZE - 1) / PAGE_SIZE <= len - i) {
            // the entire mapping (+ some), no need to split
            pages_to_change = (vmr->len + PAGE_SIZE - 1) / PAGE_SIZE;
        } else {
            if (vmr->prot != prot)
                mmap_split_record(&current_process->vm, vmr, (len - i) * PAGE_SIZE);
            pages_to_change = len - i;
        }

        if (vmr->prot != prot) {
            vmr->prot = prot;

            for (size_t j = 0; j < pages_to_change; j++) {
                if (paging_virt_addr_to_phys(addr + (i+j)*PAGE_SIZE))
                    paging_change_flags(addr + (i+j)*PAGE_SIZE, 1, mapping_flags);
            }
        }
        i += pages_to_change;
    }

    rw_spinlock_release_write(&current_process->vm_lock);
    return 0;
}

static void munmap_vmr(struct vm_record * vmr, char was_empty) {
    inode_t * backing_inode = NULL;
    if (was_empty) {
        if (!vmr->backing_fd) {
            kfree(vmr);
            return;
        }
        backing_inode = vmr->backing_fd->inode;
        goto end;
    }
    // TODO: change when implementing rbtrees for MAP_SHARED MAP_ANON
    if (vmr->private || !vmr->backing_fd) {
        for (void * i = vmr->node.ptr; i < vmr->node.ptr + vmr->len; i += PAGE_SIZE) {
            void * phys = NULL;
            if ((phys = paging_virt_addr_to_phys(i)) == NULL) continue;
            paging_unmap_page(i);
            pffree(phys);
        }
    }
    if (!vmr->backing_fd) {
        kfree(vmr);
        return;
    }

    backing_inode = vmr->backing_fd->inode;
    kassert(backing_inode);
    if (vmr->private)
        goto end;

    rw_spinlock_acquire_write(&backing_inode->mmap_pc_lock);
    unsigned long target_offset = vmr->mapping_offset >> 12;
    for (void * i = vmr->node.ptr; i < vmr->node.ptr + vmr->len; i += PAGE_SIZE, target_offset ++) {
        if (!paging_virt_addr_to_phys(i)) continue;

        if (S_ISREG(backing_inode->mode)) {
            struct mmap_page_cache * mpc =
                (struct mmap_page_cache *)rbtree_search_exact(
                    (rbtree_t *)backing_inode->mmap_page_cache,
                    target_offset);
            if (!mpc)
                continue;
            if (__atomic_sub_fetch(&mpc->instances, 1, __ATOMIC_RELEASE) == 0) {
                if (__atomic_load_n(&mpc->dirty, __ATOMIC_ACQUIRE)) {
                    if (i + PAGE_SIZE > vmr->node.ptr + vmr->len) // last page of the mapping
                        pwrite_file(vmr->backing_fd, i, vmr->len % PAGE_SIZE, target_offset << 12);
                    else
                        pwrite_file(vmr->backing_fd, i, PAGE_SIZE, target_offset << 12);

                }

                rbtree_remove((rbtree_t**)&backing_inode->mmap_page_cache, (rbtree_t*)mpc);
                pffree(mpc->page);
                kfree(mpc);
            }
        }

        paging_unmap_page(i);
    }
    rw_spinlock_release_write(&backing_inode->mmap_pc_lock);

    end:
    __atomic_sub_fetch(&backing_inode->mmaped_instances, 1, __ATOMIC_RELEASE);
    close_file(vmr->backing_fd);
    kfree(vmr);
}

int munmap_to_vmr(struct vm_record ** vmr_tree, void *addr, size_t len, char ignore_missing, char was_empty) {
    if (len == 0 || !vmr_tree || !*vmr_tree)
        return 0;
    kassert((uintptr_t)addr % PAGE_SIZE == 0);

    len = (len + PAGE_SIZE - 1)/PAGE_SIZE;

    for (size_t i = 0; i < len;) {
        struct vm_record * vmr =
            (struct vm_record *)rbtree_search_lte((rbtree_t*)*vmr_tree, (uintptr_t)addr + i*PAGE_SIZE);

        if (!vmr || vmr->node.ptr + vmr->len < addr) {
            if (ignore_missing) {
                i++;
                continue;
            }
            return -ENOMEM;
        }

        size_t offset = (uintptr_t)addr + i*PAGE_SIZE - vmr->node.val;

        if (offset != 0) {
            vmr = mmap_split_record(vmr_tree, vmr, offset);
            kassert(vmr);
        }

        size_t pages_to_change = 0;
        if ((vmr->len + PAGE_SIZE - 1) / PAGE_SIZE <= len - i) {
            // the entire mapping (+ some), no need to split
            pages_to_change = (vmr->len + PAGE_SIZE - 1) / PAGE_SIZE;
        } else {
            mmap_split_record(vmr_tree, vmr, (len - i) * PAGE_SIZE);
            pages_to_change = len - i;
        }

        rbtree_remove((rbtree_t**)vmr_tree, (rbtree_t*)vmr);
        munmap_vmr(vmr, was_empty);

        i += pages_to_change;
    }
    return 0;
}

int sys_munmap(void *addr, size_t len) {
    if ((uintptr_t)addr % PAGE_SIZE)
        return -EINVAL;
    if (len == 0)
        return 0; // ig?

    rw_spinlock_acquire_write(&current_process->vm_lock);
    long ret = munmap_to_vmr(&current_process->vm, addr, len, 0, 0);
    rw_spinlock_release_write(&current_process->vm_lock);
    return ret;
}

void munmap_free_vm(struct vm_record * vmr, char was_empty) {
    if (!vmr)
        return;
    munmap_free_vm((struct vm_record *)vmr->node.nodes[0], was_empty);
    munmap_free_vm((struct vm_record *)vmr->node.nodes[1], was_empty);
    munmap_vmr(vmr, was_empty);
}

void munmap_all(process_t * process) {
    if (!process || !process->vm)
        return;

    void * old_cr3 = paging_get_address_space_paddr();
    paging_apply_address_space(process->address_space_paddr);
    munmap_free_vm(process->vm, 0);
    process->vm = NULL;
    paging_apply_address_space(old_cr3);
}

static void mmap_dup_vm_add(struct vm_record ** vmr, const struct vm_record * node) {
    if (!node)
        return;
    struct vm_record * new_node = kalloc(sizeof(struct vm_record));
    kassert(new_node);
    memcpy(new_node, node, sizeof(struct vm_record));
    if (new_node->backing_fd) {
        kassert(new_node->backing_fd->inode);
        __atomic_add_fetch(&new_node->backing_fd->instances, 1, __ATOMIC_ACQUIRE);
        __atomic_add_fetch(&new_node->backing_fd->inode->mmaped_instances, 1, __ATOMIC_ACQUIRE);
    }
    rbtree_add((rbtree_t**)vmr, (rbtree_t*)new_node);
    mmap_dup_vm_add(vmr, (struct vm_record*)node->node.nodes[0]);
    mmap_dup_vm_add(vmr, (struct vm_record*)node->node.nodes[1]);
}

struct vm_record * mmap_dup_vm(const struct vm_record * vmr) {
    if (!vmr)
        return NULL;
    struct vm_record * new_vmr = NULL;
    mmap_dup_vm_add(&new_vmr, vmr);
    return new_vmr;
}

char mmap_check_address(const void * addr, char writable) {
    addr = (void*)((uintptr_t)addr & ~(PAGE_SIZE - 1)); // should be aligned, but to be sure

    struct vm_record * vmr =
        (struct vm_record *)rbtree_search_lte(
            (rbtree_t *)current_process->vm, (uintptr_t)addr);
    if (!vmr || vmr->node.ptr + vmr->len <= addr)
        return 0;
    // we really can't enforce this if not PROT_NONE with the page tables themselves because x86 doesn't have R^W
    // this is just so that syscalls can return -EFAULT
    if (!(vmr->prot & PROT_READ))
        return 0;
    if (!(vmr->prot & PROT_WRITE) && writable)
        return 0;

    int mapping_flags = 0;
    if (vmr->prot)
        mapping_flags |= PTE_PDE_PAGE_USER_ACCESS;
    if (vmr->prot & PROT_WRITE)
        mapping_flags |= PTE_PDE_PAGE_WRITABLE;

    if (!vmr->backing_fd) {
        if (!paging_map((void*)addr, 1, mapping_flags))
            return 0;
        return 1;
    }

    off_t target_offset = (uintptr_t)addr - vmr->node.val + vmr->mapping_offset;

    if (!vmr->private) {
        kassert(vmr->backing_fd->inode);
        if (!S_ISREG(vmr->backing_fd->inode->mode))
            return 0;
            //panic("Missing pages for mmaped device");

        rw_spinlock_acquire_write(&vmr->backing_fd->inode->mmap_pc_lock);
        struct mmap_page_cache * mpc =
            (struct mmap_page_cache *)rbtree_search_exact(
                (rbtree_t *)vmr->backing_fd->inode->mmap_page_cache, target_offset >> 12
        );
        if (mpc) {
            if (writable)
                __atomic_store_n(&mpc->dirty, 1, __ATOMIC_RELEASE);
            __atomic_add_fetch(&mpc->instances, 1, __ATOMIC_ACQUIRE);
            paging_map_phys_addr(mpc->page, (void*)addr, mapping_flags);

            rw_spinlock_release_write(&vmr->backing_fd->inode->mmap_pc_lock);
            return 1;
        }
    }

    if (!paging_map((void*)addr, 1, mapping_flags))
        return 0;
    disable_wp();
    pread_file(vmr->backing_fd, (void*)addr, PAGE_SIZE, target_offset);
    enable_wp();

    if (!vmr->private) {
        struct mmap_page_cache * new_mpc = kalloc(sizeof(struct mmap_page_cache));
        kassert(new_mpc);
        new_mpc->dirty = writable;
        new_mpc->instances = 1;
        new_mpc->node.val = target_offset >> 12;
        new_mpc->page = paging_virt_addr_to_phys((void*)addr);
        rbtree_add((rbtree_t**)&vmr->backing_fd->inode->mmap_page_cache, (rbtree_t*)new_mpc);

        rw_spinlock_release_write(&vmr->backing_fd->inode->mmap_pc_lock);
    }
    return 1;
}

char mmap_mark_page_dirty(const void * addr) {
    addr = (void*)((uintptr_t)addr & ~(PAGE_SIZE - 1)); // should be aligned, but to be sure
    if (!paging_virt_addr_to_phys((void*)addr))
        return 0;

    struct vm_record * vmr =
            (struct vm_record *)rbtree_search_lte(
                (rbtree_t *)current_process->vm, (uintptr_t)addr);
    if (!vmr || vmr->node.ptr + vmr->len <= addr)
        return 0;
    if (!(vmr->prot & PROT_WRITE))
        return 0;

    if (vmr->backing_fd) {
        kassert(vmr->backing_fd->inode);
        if (vmr->private || !S_ISREG(vmr->backing_fd->inode->mode))
            goto end; // shouldn't happen

        off_t target_offset = (uintptr_t)addr - vmr->node.val + vmr->mapping_offset;
        target_offset >>= 12;

        struct mmap_page_cache * mpc =
            (struct mmap_page_cache *)rbtree_search_exact(
                (rbtree_t *)vmr->backing_fd->inode->mmap_page_cache,
                target_offset
        );

        if (mpc) {
            __atomic_store_n(&mpc->dirty, 1, __ATOMIC_RELEASE);
        } // else { what? }
    }

    end:
    *paging_get_pte(addr) |= PTE_PDE_PAGE_WRITABLE;
    flush_tlb_entry((void*)addr);
    return 1;
}
