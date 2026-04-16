#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>
#include "../../libc/src/include/string.h"
#include "../include/kernel.h"
#include "../include/mm/kernel_memory.h"
#include "../include/kernel_spinlock.h"
#include "../include/kernel_sched.h" // so we can use current_process as an easy way to figure out if we need to spinlock
#define KALLOC_MAGIC "KAL"

spinlock_t kalloc_lock = {0};

enum kalloc_flags {
    KALLOC_CHUNK_USED = 1,
    KALLOC_FIRST_CHUNK = 2, // prev_chunk = heap_struct_start
    KALLOC_LAST_CHUNK = 4 // next_chunk = heap_top
};

#define KALLOC_ALIGNMENT sizeof(unsigned long)

struct heap_header { // aligning so that we try to avoid alignment check
    char magic[3];
    uint8_t flags;
    void * last_access; // who last performed free/alloc on this and related structures, useful when debugging
    alignas(KALLOC_ALIGNMENT) struct heap_header * prev_chunk;
    alignas(KALLOC_ALIGNMENT) struct heap_header * next_chunk;
};

static void * kernel_heap_base = NULL;
static void * kernel_heap_top = NULL;
void kalloc_prepare(void * heap_struct_start, void * allocated_heap_top, void * maximum_heap_top) { // don't call multiple times
    kernel_heap_base = heap_struct_start;
    kernel_heap_top = maximum_heap_top;
    *(struct heap_header*)heap_struct_start = (struct heap_header) {
        .flags = KALLOC_FIRST_CHUNK | KALLOC_LAST_CHUNK,
        .prev_chunk = heap_struct_start,
        .next_chunk = allocated_heap_top
    };
    memcpy(((struct heap_header*)heap_struct_start)->magic, KALLOC_MAGIC, 3);
}

#pragma clang diagnostic ignored "-Wignored-attributes"
void * __attribute__((malloc, malloc(kfree))) kalloc(size_t size) {
    spinlock_acquire(&kalloc_lock);

    if (size % KALLOC_ALIGNMENT != 0) size = size + KALLOC_ALIGNMENT - size%KALLOC_ALIGNMENT;


    struct heap_header * current_heap_object;

    try_remapped:
    current_heap_object = kernel_heap_base;

    while (current_heap_object->flags & KALLOC_CHUNK_USED || (void*)current_heap_object->next_chunk - (void*)current_heap_object < size + sizeof(struct heap_header)*2) { // one for the current struct, one for the newly generated at the start of the next chunk
        if (memcmp(current_heap_object->magic, KALLOC_MAGIC, sizeof(KALLOC_MAGIC)-1) != 0) panic("Encountered heap object with corrupted magic!");

        if (current_heap_object->next_chunk == NULL) panic("Encountered heap object with corrupted next_chunk (null)!");
        if (current_heap_object->flags & KALLOC_LAST_CHUNK) {
            // try to extend the heap
            if ((void*)current_heap_object->next_chunk < kernel_heap_top) {
                paging_add_page(current_heap_object->next_chunk, PTE_PDE_PAGE_WRITABLE);
                current_heap_object->next_chunk = (void*)current_heap_object->next_chunk + PAGE_SIZE_NO_PAE;
                goto try_remapped;
            }
            spinlock_release(&kalloc_lock);
            return NULL; // cannot allocate
        }
        current_heap_object = current_heap_object->next_chunk;
    }

    current_heap_object->flags |= KALLOC_CHUNK_USED;

    struct heap_header * next_heap_object = (void *)current_heap_object + sizeof(struct heap_header) + size;
    *next_heap_object = (struct heap_header) {
        .flags = 0 | (current_heap_object->flags & KALLOC_LAST_CHUNK),
        .prev_chunk = current_heap_object,
        .next_chunk = current_heap_object->next_chunk,
        .last_access = __builtin_return_address(0)
    };
    memcpy(next_heap_object->magic, KALLOC_MAGIC, 3);

    if (current_heap_object->flags & KALLOC_LAST_CHUNK) current_heap_object->flags ^= KALLOC_LAST_CHUNK;
    current_heap_object->next_chunk = next_heap_object;

    if (!(next_heap_object->flags & KALLOC_LAST_CHUNK))
        next_heap_object->next_chunk->prev_chunk = next_heap_object;

    current_heap_object->last_access = __builtin_return_address(0);

    spinlock_release(&kalloc_lock);

#ifdef HEAP_POISONING
    memset((void*)current_heap_object + sizeof(struct heap_header), 'b', size);
#endif

    return (void*)current_heap_object + sizeof(struct heap_header);
}

void kfree(void * p) {
    if (p == NULL) return;
    spinlock_acquire(&kalloc_lock);

    struct heap_header * current_heap_object = (struct heap_header * ) (p - sizeof(struct heap_header));
#ifdef HEAP_POISONING
    memset(p, 'A', (void *)current_heap_object->next_chunk - p);
#endif

    if (memcmp(current_heap_object->magic, KALLOC_MAGIC, 3) != 0) panic("Tried to free a non-heap object (pointer)!");

    if (current_heap_object->prev_chunk == NULL) panic("Tried to free a heap object with corrupted prev_chunk (null)!");

    if (!(current_heap_object->flags & KALLOC_CHUNK_USED)) panic("Tried to double free a heap object!");

    current_heap_object->flags &= ~KALLOC_CHUNK_USED;
    current_heap_object->last_access = __builtin_return_address(0);

    if (current_heap_object->flags & KALLOC_FIRST_CHUNK) {
        if (current_heap_object->flags & KALLOC_LAST_CHUNK) goto end;
        if (current_heap_object->next_chunk->flags & KALLOC_CHUNK_USED) goto end;
        current_heap_object->flags |= current_heap_object->next_chunk->flags & KALLOC_LAST_CHUNK;
        current_heap_object->next_chunk = current_heap_object->next_chunk->next_chunk;

        current_heap_object->next_chunk->next_chunk->last_access = current_heap_object->last_access;

        if (!(current_heap_object->flags & KALLOC_LAST_CHUNK))
            current_heap_object->next_chunk->prev_chunk = current_heap_object; // last heap object doesn't point to an actual heap object, rather to the heap end
        goto end;
    } else if (current_heap_object->flags & KALLOC_LAST_CHUNK) {
        if (current_heap_object->prev_chunk->flags & KALLOC_CHUNK_USED) goto end;
        current_heap_object->prev_chunk->next_chunk = current_heap_object->next_chunk;
        current_heap_object->prev_chunk->flags |= KALLOC_LAST_CHUNK;

        current_heap_object->next_chunk->last_access = current_heap_object->last_access;
        current_heap_object->prev_chunk->last_access = current_heap_object->last_access;

        goto end;
    } else {
        if (!(current_heap_object->next_chunk->flags & KALLOC_CHUNK_USED)) {
            current_heap_object->flags |= current_heap_object->next_chunk->flags & KALLOC_LAST_CHUNK;

            current_heap_object->next_chunk = current_heap_object->next_chunk->next_chunk;

            if (current_heap_object->flags & KALLOC_LAST_CHUNK) goto end;

            current_heap_object->next_chunk->prev_chunk = current_heap_object;

            current_heap_object->next_chunk->last_access = current_heap_object->last_access;
        }
        if (!(current_heap_object->prev_chunk->flags & KALLOC_CHUNK_USED)) {
            current_heap_object->prev_chunk->flags |= current_heap_object->flags & KALLOC_LAST_CHUNK;

            current_heap_object->prev_chunk->next_chunk = current_heap_object->next_chunk;

            current_heap_object->prev_chunk->last_access = current_heap_object->last_access;

            if (current_heap_object->flags & KALLOC_LAST_CHUNK) goto end;

            current_heap_object->next_chunk->last_access = current_heap_object->last_access;

            current_heap_object->next_chunk->prev_chunk = current_heap_object->prev_chunk;
        }
    }

    end:
    spinlock_release(&kalloc_lock);
}


static void print_chunk_info(struct heap_header * header) {
    kprintf("kalloc: 0x%p - 0x%p (%lx), prev: 0x%p, a.by: %p, ",
        header, header->next_chunk,
        (unsigned long)header->next_chunk - (unsigned long)header - sizeof(struct heap_header),
        header->prev_chunk,
        header->last_access);
    if (header->flags & KALLOC_CHUNK_USED) kprintf("U, ");
    if (header->flags & KALLOC_FIRST_CHUNK) kprintf("FC, ");
    if (header->flags & KALLOC_LAST_CHUNK) kprintf("LC, ");
    kprintf("\n");
}

void kalloc_print_heap_objects() {
    spinlock_acquire(&kalloc_lock);
    size_t used_mem = 0;
    struct heap_header * current_heap_object = kernel_heap_base;

    while (!(current_heap_object->flags & KALLOC_LAST_CHUNK)) {
        print_chunk_info(current_heap_object);
        if (current_heap_object->next_chunk->flags & KALLOC_CHUNK_USED)
            used_mem += (unsigned long)current_heap_object->next_chunk -
            (unsigned long)current_heap_object -
            sizeof(struct heap_header);
        current_heap_object = current_heap_object->next_chunk;
    }
    print_chunk_info(current_heap_object);
    kprintf("kalloc: Total usage: %lu/0x%lx\n", used_mem, used_mem);
    spinlock_release(&kalloc_lock);
}

size_t kalloc_get_free_memory() {
    spinlock_acquire(&kalloc_lock);
    
    size_t occupied_mem = 0;
    struct heap_header * current_heap_object = kernel_heap_base;

    while (!(current_heap_object->flags & KALLOC_LAST_CHUNK)) {
        if (current_heap_object->flags & KALLOC_CHUNK_USED)
            occupied_mem += (size_t)current_heap_object->next_chunk - (size_t)current_heap_object - sizeof(struct heap_header);
        current_heap_object = current_heap_object->next_chunk;
    }
    if (current_heap_object->flags & KALLOC_CHUNK_USED)
            occupied_mem += (size_t)current_heap_object->next_chunk - (size_t)current_heap_object;

    spinlock_release(&kalloc_lock);
    return (size_t)(kernel_heap_top - kernel_heap_base) - occupied_mem;
}