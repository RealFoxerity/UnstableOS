#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "errno.h"

#define APPEND_DOUBLE_LINKED_LIST(item, list) do {  \
    (item)->next = NULL;                            \
    if ((list) == NULL) {                           \
        (list) = (item);                            \
    } else {                                        \
        (item)->prev = (list)->prev;                \
        (list)->prev->next = (item);                \
    }                                               \
    (list)->prev = (item);                          \
} while (0);

struct atfork_node {
    void (*prepare)();
    void (*parent)();
    void (*child)();
    struct atfork_node *next, *prev;
};

static struct atfork_node * __atfork_nodes = NULL;
static pthread_mutex_t __atfork_nodes_lock = PTHREAD_MUTEX_INITIALIZER;

int pthread_atfork(void (*prepare)(), void (*parent)(), void (*child)()) {
    pthread_mutex_lock(&__atfork_nodes_lock);

    struct atfork_node * new = malloc(sizeof(struct atfork_node));
    if (!new) {
        ___set_errno(ENOMEM);
        return -1;
    }

    new->prepare = prepare;
    new->parent = parent;
    new->child = child;
    APPEND_DOUBLE_LINKED_LIST(new, __atfork_nodes);

    pthread_mutex_unlock(&__atfork_nodes_lock);
    return 0;
}

// -1 to call prepare hooks
// and as per usual fork semantics, 0 for child, anything for parent
void __atfork_handler(pid_t new_pid) {
    if (__atfork_nodes == NULL) return;

    pthread_mutex_lock(&__atfork_nodes_lock);

    if (new_pid == -1) {
        struct atfork_node * node = __atfork_nodes->prev;
        do {
            if (node->prepare) node->prepare();
            node = node->prev;
        } while (node != __atfork_nodes);
    } else if (new_pid == 0) {
        struct atfork_node * node = __atfork_nodes;
        while (node != NULL) {
            if (node->child) node->child();
            node = node->next;
        }
    } else {
        struct atfork_node * node = __atfork_nodes;
        while (node != NULL) {
            if (node->parent) node->parent();
            node = node->next;
        }
    }

    pthread_mutex_unlock(&__atfork_nodes_lock);
}