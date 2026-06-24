// a copy of kalloc
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define MALLOC_MAGIC "MAL"

enum malloc_flags {
    MALLOC_CHUNK_USED = 1,
    MALLOC_FIRST_CHUNK = 2, // prev_chunk = heap_struct_start
    MALLOC_LAST_CHUNK = 4 // next_chunk = heap_top
};

#define MALLOC_ALIGNMENT sizeof(unsigned long)
#define MALLOC_OOM_INCREASE (0x10000)
struct malloc_heap_header { // aligning so that we try to avoid alignment check
    char magic[3];
    uint8_t flags;
    alignas(MALLOC_ALIGNMENT) struct malloc_heap_header * prev_chunk;
    alignas(MALLOC_ALIGNMENT) struct malloc_heap_header * next_chunk;
};

static void * heap_base = NULL;

pthread_mutex_t allocator_mutex = PTHREAD_MUTEX_INITIALIZER;
void malloc_prepare(void * heap_struct_start, void * heap_top) { // don't call multiple times
    heap_base = heap_struct_start;
    *(struct malloc_heap_header*)heap_struct_start = (struct malloc_heap_header) {
        .flags = MALLOC_FIRST_CHUNK | MALLOC_LAST_CHUNK,
        .prev_chunk = heap_struct_start,
        .next_chunk = heap_top
    };
    memcpy(((struct malloc_heap_header*)heap_struct_start)->magic, MALLOC_MAGIC, 3);
}

void * __attribute__((malloc, malloc(free))) malloc(size_t size) {
    pthread_mutex_lock(&allocator_mutex);
    if (size % MALLOC_ALIGNMENT != 0) size = size + MALLOC_ALIGNMENT - size%MALLOC_ALIGNMENT;

    struct malloc_heap_header * current_heap_object;
    again:
    current_heap_object = heap_base;
    while (current_heap_object->flags & MALLOC_CHUNK_USED || (void *)current_heap_object->next_chunk - (void *)current_heap_object < size + sizeof(struct malloc_heap_header)*2) { // one for the current struct, one for the newly generated at the start of the next chunk
        if (current_heap_object->next_chunk == NULL) {
            printf("malloc() current_heap_object->next_chunk == NULL\n");
            exit(255);
        }
        if (current_heap_object->flags & MALLOC_LAST_CHUNK) {
            if (sbrk(MALLOC_OOM_INCREASE) == (void *)-1) {
                pthread_mutex_unlock(&allocator_mutex);
                return NULL; // -ENOMEM usually
            }
            goto again;
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

    if (!(next_heap_object->flags & MALLOC_LAST_CHUNK))
        next_heap_object->next_chunk->prev_chunk = next_heap_object;

    pthread_mutex_unlock(&allocator_mutex);
    return (void*)current_heap_object + sizeof(struct malloc_heap_header);
}
void * __attribute__((malloc, malloc(free))) calloc(size_t nelem, size_t elsize) {
    void * ret = malloc(nelem*elsize);
    if (ret == NULL) return NULL;

    memset(ret, 0, nelem*elsize);
    return ret;
}
void free(void * p) {
    if (p == NULL) return;
    pthread_mutex_lock(&allocator_mutex);
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
    pthread_mutex_unlock(&allocator_mutex);
}

void * realloc(void * p, size_t size) {
    if (size == 0) {
        free(p);
        return NULL;
    }
    if (p == NULL) {
        return malloc(size);
    }

    size_t old_size = 0;
    pthread_mutex_lock(&allocator_mutex);
    struct malloc_heap_header * hdr = p - sizeof(struct malloc_heap_header);
    old_size = hdr->next_chunk - hdr;
    pthread_mutex_unlock(&allocator_mutex);

    void * new_chunk = malloc(size);
    if (new_chunk == NULL) return NULL;

    memcpy(new_chunk, p, old_size > size ? size : old_size);

    free(p);
    return new_chunk;
}

static void print_chunk_info(struct malloc_heap_header * header) {
    printf("malloc: Heap 0x%p - 0x%p, size %lx, prev: 0x%p, ", header, header->next_chunk, (unsigned long)header->next_chunk - (unsigned long)header - sizeof(struct malloc_heap_header), header->prev_chunk);
    if (header->flags & MALLOC_CHUNK_USED) printf("U, ");
    if (header->flags & MALLOC_FIRST_CHUNK) printf("FC, ");
    if (header->flags & MALLOC_LAST_CHUNK) printf("LC, ");
    printf("\n");
}

void malloc_print_heap_objects() {
    pthread_mutex_lock(&allocator_mutex);
    struct malloc_heap_header * current_heap_object = heap_base;

    while (!(current_heap_object->flags & MALLOC_LAST_CHUNK)) {
        print_chunk_info(current_heap_object);
        current_heap_object = current_heap_object->next_chunk;
    }

    print_chunk_info(current_heap_object);
    pthread_mutex_unlock(&allocator_mutex);
}