#include <mm/kernel_memory.h>
#include <kernel.h>
#include <string.h>
#include "rbtree.h"


#include <stdlib.h>

// inspired by
// https://en.wikipedia.org/wiki/Red%E2%80%93black_tree
// thank you kind stranger who made that article <3

rbtree_t * root = NULL;

static const rbtree_t * bstree_min(const rbtree_t *node) {
    while (node->nodes[0])
        node = node->nodes[0];
    return node;
}

static const rbtree_t * bstree_successor(const rbtree_t *tree, const rbtree_t *node) {
    if (__builtin_expect(node->nodes[1] != NULL, 1))
        return bstree_min(node->nodes[1]);
    rbtree_t * parent = (void*)(node->parent & ~1);
    while (parent && node == parent->nodes[1]) {
        node = parent;
        parent = (void*)(parent->parent & ~1);
    }
    return parent;
}

static void bstree_rotate(rbtree_t **tree, rbtree_t *node, int dir) {
    rbtree_t *parent = (void*)(node->parent & ~1);
    rbtree_t *root   = node->nodes[!dir];
    if (root == NULL) return;
    rbtree_t *child  = root->nodes[dir];

    node->nodes[!dir] = child;

    if (child)
        child->parent = (uintptr_t)node | (child->parent & 1);

    root->nodes[dir] = node;

    root->parent = (uintptr_t)parent | (root->parent & 1);
    node->parent = (uintptr_t)root   | (node->parent & 1);
    if (parent)
        parent->nodes[node == parent->nodes[1]] = root;
    else
        *tree = root;
}

rbtree_t * rbtree_search_exact(const rbtree_t * tree, unsigned long val) {
    while (tree && tree->val != val)
        tree = tree->nodes[tree->val < val];
    return (rbtree_t*)tree;
}

rbtree_t * rbtree_search_lte(const rbtree_t * tree, unsigned long val) {
    const rbtree_t * best = NULL;
    while (tree) {
        if (tree->val < val) {
            best = tree;
            tree = tree->nodes[1];
        } else if (tree->val == val) {
            best = tree;
            break;
        } else tree = tree->nodes[0];
    }
    return (rbtree_t *)best;
}

rbtree_t * rbtree_search_gte(const rbtree_t * tree, unsigned long val) {
    const rbtree_t * best = NULL;
    while (tree) {
        if (tree->val < val)
            tree = tree->nodes[1];
        else {
            best = tree;
            if (tree->val == val)
                break;
            tree = tree->nodes[0];
        }
    }
    return (rbtree_t *)best;
}

void rbtree_add(rbtree_t **tree, rbtree_t *node) {
    kassert(tree && node);
    // basic bst add
    node->parent = 0;
    node->nodes[0] = node->nodes[1] = NULL;
    if (*tree == NULL) {
        *tree = node;
        return;
    }

    rbtree_t *cur = *tree;
    rbtree_t *prev = *tree;
    int idx = 0;
    while (cur) {
        prev = cur;
        idx = cur->val < node->val;
        cur = cur->nodes[idx];
    }
    prev->nodes[idx] = node;
    node->parent = (uintptr_t)prev | 1;

    // solve the rbt
    do {
        if (!(prev->parent & 1)) // is black
            return;

        rbtree_t *gp = (void*)(prev->parent & ~1);
        if (!gp) {
            prev->parent &= ~1;
            return;
        }

        idx = prev == gp->nodes[0] ? 0 : 1;
        kassert(gp->nodes[idx] == prev);
        rbtree_t * unc = gp->nodes[!idx];
        if (!unc || !(unc->parent & 1)) {
            if (node == prev->nodes[!idx]) {
                bstree_rotate(tree, prev, idx);
                prev = gp->nodes[idx];
            }

            bstree_rotate(tree, gp, !idx);
            prev->parent &= ~1;
            gp->parent   |= 1;
            return;
        }

        prev->parent &= ~1;
        unc->parent  &= ~1;
        gp->parent   |= 1;
        node = gp;
        prev = (void*)(gp->parent & ~1);
    } while (prev);
    (*tree)->parent &= ~1;
}

void rbtree_remove(rbtree_t **tree, const rbtree_t *node) {
    kassert(tree && *tree && node);
    rbtree_t * parent = (void*)(node->parent & ~1);
    rbtree_t * replacement = NULL;
    if (node->nodes[0] == NULL || node->nodes[1] == NULL) {
        int idx = node->nodes[0] ? 0 : 1;
        replacement = node->nodes[idx];

        goto relink;
    }

    replacement = (rbtree_t*)bstree_successor(*tree, node);
    // succ can never be NULL, it's gonna be at least node->nodes[1]
    rbtree_t * succp = (void*)(replacement->parent & ~1);
    // minimum is always the leftmost node, only right node could be still here
    // so relink to the successor's parent
    if (replacement->nodes[1]) {
        replacement->nodes[1]->parent = (uintptr_t)succp;
        replacement->nodes[1]->parent |= 1;
    }
    // assuming the bst is not broken, can't be NULL either
    // second case is only possible if succ is direct descendant of succp
    if (__builtin_expect(succp->nodes[0] == replacement, 1))
        succp->nodes[0] = replacement->nodes[1];
    else
        succp->nodes[1] = replacement->nodes[1];
    replacement->nodes[0] = node->nodes[0];
    replacement->nodes[1] = node->nodes[1];

    node->nodes[0]->parent = (uintptr_t)replacement | (node->nodes[0]->parent & 1);
    node->nodes[1]->parent = (uintptr_t)replacement | (node->nodes[1]->parent & 1);

    relink:
    if (replacement)
        replacement->parent = (uintptr_t)parent | (replacement->parent & 1);
    if (node == *tree) {
        *tree = replacement;
        return;
    }

    int idx = 0;
    if (parent->nodes[0] == node)
        parent->nodes[0] = replacement;
    else {
        parent->nodes[1] = replacement;
        idx = 1;
    }

    if (replacement) {
        replacement->parent &= ~1;
        return;
    }
    if (node->parent & 1)
        return;

    // solve the rbt
    do {
        rbtree_t * sibling = parent->nodes[!idx];

        if (sibling->parent & 1) {
            bstree_rotate(tree, parent, idx);
            parent->parent  |= 1;
            sibling->parent &= ~1;
            sibling = sibling->nodes[idx]; // cnephew
        }

        rbtree_t * dnephew = sibling->nodes[!idx];
        rbtree_t * cnephew = sibling->nodes[ idx];

        sibling->parent &= ~1;
        uintptr_t original_color = parent->parent & 1;
        parent->parent  &= ~1;

        if (dnephew && dnephew->parent & 1) {
            bstree_rotate(tree, parent, idx);
            sibling->parent |= original_color;
            dnephew->parent &= ~1;
            return;
        }
        if (cnephew && cnephew->parent & 1) {
            bstree_rotate(tree, sibling, !idx);
            bstree_rotate(tree, parent, idx);
            cnephew->parent &= ~1;
            cnephew->parent |= original_color;
            return;
        }

        sibling->parent |= 1;

        if (original_color)
            return;

        node = parent;

        parent = (void *) (parent->parent & ~1);
        idx = parent && parent->nodes[1] == node;
    } while (parent);
}

void rbtree_free(rbtree_t * tree) {
    if (!tree)
        return;

    rbtree_free(tree->nodes[0]);
    rbtree_free(tree->nodes[1]);

    kfree(tree);
}

/*
void print_rbtree(const rbtree_t *tree) {
    static int depth = -1;
    depth++;
    for (int i = 0; i < depth - 1; i++) {
        kprintf("-");
    }
    if (depth)
        kprintf("|");
    kprintf(" %lx - %c\n", tree->val, tree->parent & 1 ? 'R':'B');

    if (tree->nodes[0]) {
        print_rbtree(tree->nodes[0]);
    }
    if (tree->nodes[1]) {
        print_rbtree(tree->nodes[1]);
    }
    depth--;
}

void test_rbtree() {
    kprintf("haii\n");

    for (int i = 0; i < 1024; i++) {
        rbtree_t * node = kalloc(sizeof(rbtree_t));
        memset(node, 0, sizeof(rbtree_t));
        node->val = rand();

        rbtree_add(&root, node);
    }
    rbtree_remove(&root, rbtree_search_exact(root, 0xEF917D2));
    print_rbtree(root);

    while (1) {}
}*/