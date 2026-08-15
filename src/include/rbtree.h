#ifndef RBTREE_H
#define RBTREE_H
#include <stdint.h>

// it would be better to have a function pointer to get the value, or use offsetof()
// but since we're targeting the 486 where the cacheline is 16 bytes, and we get 0 pipelining and superscalar stuff
// it would absolutely nuke the performance as compared to using this debatable method
struct rbtree_t {
    uintptr_t parent; // bit 0 = color
    struct rbtree_t *nodes[2]; // left, right
    union {
        unsigned long val;
        void * ptr;
    };
} typedef rbtree_t;


rbtree_t * rbtree_search_exact(const rbtree_t * tree, unsigned long val);
rbtree_t * rbtree_search_lte(const rbtree_t * tree, unsigned long val);
rbtree_t * rbtree_search_gte(const rbtree_t * tree, unsigned long val);

void rbtree_add(rbtree_t **tree, rbtree_t *node);
void rbtree_remove(rbtree_t **tree, const rbtree_t *node);
void rbtree_free(rbtree_t * tree);
#endif