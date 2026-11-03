#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_spinlock.h"

//#define MAX_ORDER 30 // 1G max heap size
#define MAX_ORDER 25 // 32M max heap size, the KERNEL_HEAP_SIZE size
#define MIN_ORDER 6  // 64 bytes
#define MIN_ALIGN ((1 << MIN_ORDER) - 1)
#define NORDER (MAX_ORDER - MIN_ORDER + 1)
#define MAX_ALLOC (1<<MAX_ORDER)

static unsigned long used_bitmap [(1<<(NORDER - 1))/(8*sizeof(unsigned long))];
static unsigned long split_bitmap[(1<<(NORDER - 1))/(8*sizeof(unsigned long))];

struct free_block {
    struct free_block *prev, *next;
};
static struct free_block * buckets[NORDER];

static size_t free_mem = 1 << MAX_ORDER;
spinlock_t kalloc_lock = {0};

void kalloc_prepare() {
    buckets[NORDER - 1] = KERNEL_HEAP_BASE;
    kassert(paging_map(KERNEL_HEAP_BASE, PAGE_SIZE, PTE_PDE_PAGE_WRITABLE));
    buckets[NORDER - 1]->next = NULL;
    buckets[NORDER - 1]->prev = KERNEL_HEAP_BASE;
}

static unsigned long bm_get(const unsigned long * bm, int idx) {
    return !!(bm[idx/(8*sizeof(unsigned long))] & (1 << (idx%(8*sizeof(unsigned long)))));
}
static void bm_set(unsigned long * bm, int idx, char val) {
    bm[idx/(8*sizeof(unsigned long))] &= ~(1 << (idx%(8*sizeof(unsigned long))));
    bm[idx/(8*sizeof(unsigned long))] |= !!val << (idx%(8*sizeof(unsigned long)));
}
static unsigned long bm_toggle(unsigned long * bm, int idx) {
    return !!((bm[idx/(8*sizeof(unsigned long))] ^= 1 << (idx%(8*sizeof(unsigned long)))) & (1 << (idx%(8*sizeof(unsigned long)))));
}

static int get_idx_in_lvl(const void * block, int order) {
    order = NORDER - 1 - order;
    return (block - KERNEL_HEAP_BASE)/((1 << MAX_ORDER) / (1 << order));
}
static int get_idx(const void * block, int order) {
    int lvl_idx = get_idx_in_lvl(block, order);
    order = NORDER - 1 - order; // our order is 0 = smallest, however this idx logic needs 0 = largest
    return (1 << order) - 1 + lvl_idx;
}

static int get_order(const void * block) {
    for (int i = 0; i < NORDER - 1; i++) {
        if (bm_get(split_bitmap, get_idx(block, i + 1)))
            return i;
    }
    return NORDER - 1;
}

static void split_block(const void * block, int order) {
    if (order < NORDER-1) {
        int idx = get_idx(block, order+1);
        bm_toggle(used_bitmap, idx);
    }
    bm_set(split_bitmap, get_idx(block, order), 1);
}

void * __attribute__((malloc, malloc(kfree))) kalloc(size_t size) {
    if (size == 0 || size > MAX_ALLOC)
        return NULL;

    int order = (int)(8*sizeof(size_t) - __builtin_clz(size) - 1);
    if (size & ((1<<order) - 1))
        order++;
    if (order < MIN_ORDER)
        order = MIN_ORDER;
    order -= MIN_ORDER;
    void * block;
    spinlock_acquire(&kalloc_lock);
    if (buckets[order]) {
        if (buckets[order]->next)
            buckets[order]->next->prev = buckets[order]->prev;
        block = buckets[order];
        buckets[order] = buckets[order]->next;

        ret:
        if (order < NORDER - 1)
            bm_toggle(used_bitmap, get_idx(block, order+1));

        spinlock_release(&kalloc_lock);
        __atomic_sub_fetch(&free_mem, 1 << (order + MIN_ORDER), __ATOMIC_RELAXED);

        for (size_t i = 0; i < 1 << (order + MIN_ORDER); i += PAGE_SIZE)
            if (!paging_get_pte(block + i))
                paging_add_page(block + i, PTE_PDE_PAGE_WRITABLE);
#ifdef HEAP_POISONING
        memset(block, 'b', 1 << (order + MIN_ORDER));
#endif
        return block;
    }

    for (int i = order; i < NORDER; i++) {
        if (!buckets[i])
            continue;

        if (buckets[i]->next)
            buckets[i]->next->prev = buckets[i]->prev;
        block = buckets[i];
        buckets[i] = buckets[i]->next;
        //bm_toggle(used_bitmap, get_idx(block, i+1)); // done by split_block

        for (int j = i; j > order; j--) {
            split_block(block, j);
            void * tgt = block + (1 << (MIN_ORDER + j - 1));
            if (!paging_get_pte(tgt))
                kassert(paging_map(tgt, PAGE_SIZE, PTE_PDE_PAGE_WRITABLE));
            APPEND_DOUBLE_LINKED_LIST((struct free_block*)tgt, buckets[j-1]);
        }
        goto ret;
    }
    spinlock_release(&kalloc_lock);
    return NULL;
}

void kfree(void * p) {
    if (!p) return;
    kassert(!((uintptr_t)(p-KERNEL_HEAP_BASE) & MIN_ALIGN));

    spinlock_acquire(&kalloc_lock);

    int order = get_order(p);
    kassert(!((uintptr_t)(p-KERNEL_HEAP_BASE) & ((1<<(MIN_ORDER + order)) - 1)));

#ifdef HEAP_POISONING
    memset(p, 'A', 1 << (order + MIN_ORDER));
#endif

    for (int i = order; i < NORDER; i++) {
        if (i >= NORDER - 1 || bm_toggle(used_bitmap, get_idx(p, i+1))) {
            // the other block is still used, mark this one as free
            APPEND_DOUBLE_LINKED_LIST((struct free_block *)p, buckets[i]);
            spinlock_release(&kalloc_lock);
            __atomic_add_fetch(&free_mem, 1 << (i + MIN_ORDER), __ATOMIC_RELAXED);
            return;
        }
        // set this layer as no longer split
        if (i < NORDER - 1)
            bm_set(split_bitmap, get_idx(p, i+1), 0);

        // the right block
        if (get_idx_in_lvl(p, i) % 2) {
            p -= 1 << (MIN_ORDER + i);
            UNLINK_DOUBLE_LINKED_LIST((struct free_block *)p, buckets[i]);
        } else
            UNLINK_DOUBLE_LINKED_LIST((struct free_block *)(p + (1 << (MIN_ORDER + i))), buckets[i]);
    }
    __builtin_unreachable();
}

void * krealloc(void * p, size_t size) {
    if (size == 0) {
        kfree(p);
        return NULL;
    }
    if (p == NULL) {
        return kalloc(size);
    }

    int new_order = (int)(8*sizeof(size_t) - __builtin_clz(size) - 1);
    if (size & ((1<<new_order) - 1))
        new_order++;
    if (new_order < MIN_ORDER)
        new_order = MIN_ORDER;
    new_order -= MIN_ORDER;

    size_t old_size = 0;
    spinlock_acquire(&kalloc_lock);
    int old_order = get_order(p);
    old_size = 1<<(MIN_ORDER + old_order);
    spinlock_release(&kalloc_lock);

    if (old_order == new_order)
        return p;

    void * new_chunk = kalloc(size);
    if (new_chunk == NULL) return NULL;

    memcpy(new_chunk, p, old_size > size ? size : old_size);

    kfree(p);
    return new_chunk;
}

size_t kalloc_get_free_memory() {
    return __atomic_load_n(&free_mem, __ATOMIC_RELAXED);
}

// libc weak overrides
void __attribute__((weak)) free(void * p) {
    kfree(p);
}
void * __attribute__((malloc, malloc(free), weak)) malloc(size_t size) {
    return kalloc(size);
}
void * __attribute__((weak)) realloc(void * p, size_t size) {
    return krealloc(p, size);
}