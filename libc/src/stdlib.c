#include <stdlib.h>
#include <unistd.h>
#include <UnstableOS/syscalls.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>

static uint32_t ___internal_rand_state = 1;

uint32_t rand() {
    if (__builtin_expect(___internal_rand_state == 0, 0))
        ___internal_rand_state = 0xDEADBEEF;

    // https://en.wikipedia.org/wiki/Xorshift

    ___internal_rand_state ^= ___internal_rand_state << 13;
    ___internal_rand_state ^= ___internal_rand_state >> 17;
    ___internal_rand_state ^= ___internal_rand_state << 5;

    return ___internal_rand_state % RAND_MAX;
}

void srand(uint32_t seed) {___internal_rand_state = seed;}

#define ATEXIT_HANDLERS_PER_NODE 32

struct atexit_node {
    void (*func[ATEXIT_HANDLERS_PER_NODE])();
    int last_func_idx; // last free slot
    struct atexit_node * next;
};

static struct atexit_node __atexit_first_node;
static struct atexit_node * __atexit_nodes = NULL;
static pthread_mutex_t __atexit_nodes_lock = PTHREAD_MUTEX_INITIALIZER;

int atexit(void (*func)()) {
    pthread_mutex_lock(&__atexit_nodes_lock);
    if (__atexit_nodes == NULL) {
        memset(&__atexit_first_node, 0, sizeof(__atexit_first_node));
        __atexit_nodes = &__atexit_first_node;
    }
    if (__atexit_nodes->last_func_idx == ATEXIT_HANDLERS_PER_NODE) {
        struct atexit_node * node = malloc(sizeof(struct atexit_node));
        if (node == NULL) {
            ___set_errno(ENOMEM);
            pthread_mutex_unlock(&__atexit_nodes_lock);
            return -1;
        }
        memset(node, 0, sizeof(*node));
        node->next = __atexit_nodes;
        __atexit_nodes = node;
    }
    __atexit_nodes->func[__atexit_nodes->last_func_idx] = func;
    __atexit_nodes->last_func_idx ++;
    pthread_mutex_unlock(&__atexit_nodes_lock);
    return 0;
}

static void __call_atexit() {
    pthread_mutex_lock(&__atexit_nodes_lock); // leaking on purpose because we're exiting anyway
    while (__atexit_nodes) {
        for (int i = 0; i < __atexit_nodes->last_func_idx; i++) {
            __atexit_nodes->func[i]();
        }
        __atexit_nodes = __atexit_nodes->next;
        // no need for free on exit
    }
}

extern void __stdio_deinit();
void exit(long exit_code) {
    static char is_exiting = 0;
    if (__atomic_load_n(&is_exiting, __ATOMIC_ACQUIRE)) {
        // a different thread and/or atexit handler called exit, UB
        _exit(exit_code);
    }
    __atomic_store_n(&is_exiting, 1, __ATOMIC_RELEASE);

    __call_atexit();
    __stdio_deinit();
    _exit(exit_code);
}

void _exit(long exit_code) {
    syscall(SYSCALL_EXIT, exit_code);
    __builtin_unreachable();
}

void _Exit(long exit_code) {
    _exit(exit_code);
}

void abort() {
    syscall(SYSCALL_ABORT);
    __builtin_unreachable();
}


void yield() {
    syscall(SYSCALL_YIELD);
}

pid_t wait(int * wstatus) {
    return waitpid(-1, wstatus, 0);
}
pid_t waitpid(pid_t pid, int * wstatus, int options) {
    pid_t ret = syscall(SYSCALL_WAITPID, pid, wstatus, options);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int waitid(idtype_t idtype, id_t id, siginfo_t * infop, int options) {
    int ret = syscall(SYSCALL_WAITID, idtype, id, infop, options);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
extern char ** environ;
char * getenv(const char * name) {
    for (int i = 0; environ[i] != NULL; i++) {
        char * equals = strchrnul(environ[i], '=');
        if (strncmp(name, environ[i], equals - environ[i]) == 0) {
            return equals + 1;
        }
    }
    return NULL;
}