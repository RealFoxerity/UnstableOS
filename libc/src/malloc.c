// a copy of kalloc
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>

#include "include/string.h"
#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/uthreads.h"

#define MALLOC_MAGIC "MAL"

enum malloc_flags {
    MALLOC_CHUNK_USED = 1,
    MALLOC_FIRST_CHUNK = 2, // prev_chunk = heap_struct_start
    MALLOC_LAST_CHUNK = 4 // next_chunk = heap_top
};

#define MALLOC_ALIGNMENT sizeof(unsigned long)

struct malloc_heap_header { // aligning so that we try to avoid alignment check
    char magic[3];
    uint8_t flags;
    alignas(MALLOC_ALIGNMENT) struct malloc_heap_header * prev_chunk;
    alignas(MALLOC_ALIGNMENT) struct malloc_heap_header * next_chunk;
};

static void * heap_base = NULL;

mutex_t allocator_mutex;
void malloc_prepare(void * heap_struct_start, void * heap_top) { // don't call multiple times
    allocator_mutex = mutex_init();
    heap_base = heap_struct_start;
    *(struct malloc_heap_header*)heap_struct_start = (struct malloc_heap_header) {
        .flags = MALLOC_FIRST_CHUNK | MALLOC_LAST_CHUNK,
        .prev_chunk = heap_struct_start,
        .next_chunk = heap_top
    };
    memcpy(((struct malloc_heap_header*)heap_struct_start)->magic, MALLOC_MAGIC, 3);
}

void * __attribute__((malloc, malloc(free))) malloc(size_t size) {
    mutex_lock(allocator_mutex);
    if (size % MALLOC_ALIGNMENT != 0) size = size + MALLOC_ALIGNMENT - size%MALLOC_ALIGNMENT;

    struct malloc_heap_header * current_heap_object = heap_base;
    while (current_heap_object->flags & MALLOC_CHUNK_USED || (void *)current_heap_object->next_chunk - (void *)current_heap_object < size + sizeof(struct malloc_heap_header)*2) { // one for the current struct, one for the newly generated at the start of the next chunk
        if (current_heap_object->next_chunk == NULL) {
            printf("malloc() current_heap_object->next_chunk == NULL\n");
            exit(255);
        }
        if (current_heap_object->flags & MALLOC_LAST_CHUNK) {
            mutex_unlock(allocator_mutex);
            return NULL; // not enough free space on the stack and considering we do overcommitment, there's nothing we can do
        }
        current_heap_object = current_heap_object->next_chunk;
    }
    current_heap_object->flags |= MALLOC_CHUNK_USED;

    struct malloc_heap_header * next_heap_object = (struct malloc_heap_header*)((void *)current_heap_object + sizeof(struct malloc_heap_header) + size);

    *next_heap_object = (struct malloc_heap_header) {
        .flags = 0 | (current_heap_object->flags & MALLOC_LAST_CHUNK),
        .prev_chunk = current_heap_object,
        .next_chunk = current_heap_object->next_chunk
    };
    memcpy(next_heap_object->magic, MALLOC_MAGIC, 3);

    if (current_heap_object->flags & MALLOC_LAST_CHUNK) current_heap_object->flags ^= MALLOC_LAST_CHUNK;
    current_heap_object->next_chunk = next_heap_object;
    mutex_unlock(allocator_mutex);
    return (void*)current_heap_object + sizeof(struct malloc_heap_header);
}

void free(void * p) {
    if (p == NULL) return;
    mutex_lock(allocator_mutex);
    struct malloc_heap_header * current_heap_object = (struct malloc_heap_header * ) (p - sizeof(struct malloc_heap_header));

    if (memcmp(current_heap_object->magic, MALLOC_MAGIC, 3) != 0) {
        printf("free() tried to free non-heap object\n");
        exit(255);
    }

    if (current_heap_object->prev_chunk == NULL) {
        printf("free() current_heap_object->prev_chunk == NULL\n");
        exit(255);
    }

    if (!(current_heap_object->flags & MALLOC_CHUNK_USED)) {
        printf("free() double free\n");
        exit(255);
    }

    current_heap_object->flags &= ~MALLOC_CHUNK_USED;

    if (current_heap_object->flags & MALLOC_FIRST_CHUNK) {
        if (current_heap_object->flags & MALLOC_LAST_CHUNK) goto end;
        if (current_heap_object->next_chunk->flags & MALLOC_CHUNK_USED) goto end;
        

        current_heap_object->flags |= current_heap_object->next_chunk->flags & MALLOC_LAST_CHUNK;
        current_heap_object->next_chunk = current_heap_object->next_chunk->next_chunk;
        
        if (!(current_heap_object->flags & MALLOC_LAST_CHUNK))
            current_heap_object->next_chunk->prev_chunk = current_heap_object;

        goto end;
    } else if (current_heap_object->flags & MALLOC_LAST_CHUNK) {
        if (current_heap_object->prev_chunk->flags & MALLOC_CHUNK_USED) goto end;
        current_heap_object->prev_chunk->next_chunk = current_heap_object->next_chunk;
        current_heap_object->prev_chunk->flags |= MALLOC_LAST_CHUNK;
        goto end;
    } else {
        if (!(current_heap_object->next_chunk->flags & MALLOC_CHUNK_USED)) {
            current_heap_object->flags |= current_heap_object->next_chunk->flags & MALLOC_LAST_CHUNK;

            current_heap_object->next_chunk = current_heap_object->next_chunk->next_chunk;
            if (current_heap_object->flags & MALLOC_LAST_CHUNK) goto end;

            current_heap_object->next_chunk->prev_chunk = current_heap_object;
        }
        if (!(current_heap_object->prev_chunk->flags & MALLOC_CHUNK_USED)) {
            current_heap_object->prev_chunk->flags |= current_heap_object->flags & MALLOC_LAST_CHUNK;

            current_heap_object->prev_chunk->next_chunk = current_heap_object->next_chunk;
            if (current_heap_object->flags & MALLOC_LAST_CHUNK) goto end;

            current_heap_object->next_chunk->prev_chunk = current_heap_object->prev_chunk;
        }
    }

    end:
    mutex_unlock(allocator_mutex);
}

static void print_chunk_info(struct malloc_heap_header * header) {
    printf("malloc: Heap 0x%p - 0x%p, size %lx, prev: 0x%p, ", header, header->next_chunk, (unsigned long)header->next_chunk - (unsigned long)header - sizeof(struct malloc_heap_header), header->prev_chunk);
    if (header->flags & MALLOC_CHUNK_USED) printf("U, ");
    if (header->flags & MALLOC_FIRST_CHUNK) printf("FC, ");
    if (header->flags & MALLOC_LAST_CHUNK) printf("LC, ");
    printf("\n");
}

void malloc_print_heap_objects() {
    mutex_lock(allocator_mutex);
    struct malloc_heap_header * current_heap_object = heap_base;

    while (!(current_heap_object->flags & MALLOC_LAST_CHUNK)) {
        print_chunk_info(current_heap_object);
        current_heap_object = current_heap_object->next_chunk;
    }
    
    print_chunk_info(current_heap_object);
    mutex_unlock(allocator_mutex);
}