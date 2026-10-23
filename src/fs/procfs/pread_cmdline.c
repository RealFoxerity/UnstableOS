#include "mm/kernel_memory.h"
#include "mm/pmm.h"
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include "rbtree.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "procfs.h"
#include "lowlevel.h"
#include "kernel.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

// TODO: handle mmaped regions, figure out a way to avoid process locking

#define MAX_INSTANCES 32 // to not starve out the short page mapper, we need 5 pages per read

static semaphore_t cmdline_read_sem = {.value = MAX_INSTANCES};

// if comm = 1, then read only argv[0]
ssize_t procfs_pread_cmdline(process_t * proc, void * buf, size_t n, off_t offset, char comm) {
    kassert(proc);
    kassert(buf);
    if (n == 0)
        return 0;
    if (offset >= ARG_MAX)
        return 0;
    if (!proc->address_space_paddr)
        return -EBADF;
    if (!proc->argv)
        return 0;
    if ((unsigned long)proc->argv & (sizeof(char**)-1)) {
        kprintf("procfs: Warning: unaligned argv on process %ld, execve bug?\n", proc->pid);
        return -EIO; // what do I return here?
    }

    if (check_eintr())
        return -EINTR;

    semaphore_wait(&cmdline_read_sem);

    spinlock_acquire_interruptible(&proc->lock);
    if (!__atomic_load_n(&proc->address_space_paddr, __ATOMIC_ACQUIRE)) {
        // raced with the process reaper
        spinlock_release(&proc->lock);
        semaphore_post(&cmdline_read_sem);
        return -EBADF;
    }

    ssize_t read_bytes = 0;

    PAGE_DIRECTORY_TYPE * mapped_as = paging_map_page_short(proc->address_space_paddr);
    PAGE_TABLE_TYPE * mapped_pt = NULL;
    PAGE_TABLE_TYPE * last_pt = NULL;
    void * mapped_page = NULL;
    void * last_page = NULL;

    PAGE_TABLE_TYPE * mapped_arg_pt = NULL;
    PAGE_TABLE_TYPE * last_arg_pt = NULL;
    void * mapped_arg_page = NULL;
    void * last_arg_page = NULL;

    char * out = buf;
    int narg = 0;
    const char * arg = NULL;
    while (read_bytes < n + offset) {
        if (check_eintr()) {
            read_bytes = read_bytes == 0 ? -EINTR : read_bytes;
            break;
        }

        void * tgt = proc->argv + narg;
        unsigned int pdidx = (unsigned long)tgt >> 22;
        unsigned int ptidx = ((unsigned long)tgt >> 10) & 0x3ff;
        if ((mapped_as[pdidx] & (PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS)) != (PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS))
            break;
        PAGE_TABLE_TYPE * pt = (PAGE_TABLE_TYPE *)(mapped_as[pdidx] & ~(PAGE_SIZE - 1));
        if (pt != last_pt) {
            paging_unmap_page_short(mapped_pt);
            mapped_pt = paging_map_page_short(pt);
            last_pt = pt;
        }

        if ((mapped_pt[ptidx] & (PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS)) != (PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS))
            break;
        void * page = (void *)(mapped_pt[ptidx] & ~(PAGE_SIZE - 1));
        if (page != last_page) {
            paging_unmap_page_short(mapped_page);
            mapped_page = paging_map_page_short(page);
            last_page = page;
        }

        tgt = *(char**)(mapped_page + ((unsigned long)tgt & (PAGE_SIZE - 1)));
        if (tgt == NULL)
            break;
        next_arg_page:
        pdidx = (unsigned long)tgt >> 22;
        ptidx = ((unsigned long)tgt >> 10) & 0x3ff;
        if ((mapped_as[pdidx] & (PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS)) != (PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS))
            break;
        PAGE_TABLE_TYPE * arg_pt = (PAGE_TABLE_TYPE *)(mapped_as[pdidx] & ~(PAGE_SIZE - 1));
        if (arg_pt != last_arg_pt) {
            paging_unmap_page_short(mapped_arg_pt);
            mapped_arg_pt = paging_map_page_short(arg_pt);
            last_arg_pt = arg_pt;
        }

        if ((mapped_arg_pt[ptidx] & (PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS)) != (PTE_PDE_PAGE_PRESENT | PTE_PDE_PAGE_USER_ACCESS))
            break;
        page = (void *)(mapped_pt[ptidx] & ~(PAGE_SIZE - 1));
        if (page != last_arg_page) {
            paging_unmap_page_short(mapped_arg_page);
            mapped_arg_page = paging_map_page_short(page);
            last_arg_page = page;
        }

        arg = mapped_arg_page + ((unsigned long)tgt & (PAGE_SIZE - 1));

        while (read_bytes < n + offset) {
            if ((read_bytes + offset) % 1024 == 0 && check_eintr()) {
                read_bytes = read_bytes == 0 ? -EINTR : read_bytes;
                goto end;
            }
            if (!offset) {
                *out++ = *arg;
                read_bytes++;
            } else offset--;
            if (!*arg++)
                break;

            // ran over the page end
            if (((unsigned long)arg & (PAGE_SIZE - 1)) == 0) {
                tgt = (void*)((unsigned long)(tgt + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
                goto next_arg_page;
            }
        }
        if (comm)
            break;
        narg++;
    }
    end:
    paging_unmap_page_short(mapped_page);
    paging_unmap_page_short(mapped_pt);
    paging_unmap_page_short(mapped_arg_page);
    paging_unmap_page_short(mapped_arg_pt);
    paging_unmap_page_short(mapped_as);

    spinlock_release(&proc->lock);
    semaphore_post(&cmdline_read_sem);
    return read_bytes;
}