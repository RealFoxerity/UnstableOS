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

int abs(int i) {
    if (i < 0)
        return -i;
    return i;
}

static uint32_t ___internal_rand_state = 1;

int rand() {
    if (__builtin_expect(___internal_rand_state == 0, 0))
        ___internal_rand_state = 0xDEADBEEF;

    // https://en.wikipedia.org/wiki/Xorshift

    ___internal_rand_state ^= ___internal_rand_state << 13;
    ___internal_rand_state ^= ___internal_rand_state >> 17;
    ___internal_rand_state ^= ___internal_rand_state << 5;

    return (int)(___internal_rand_state % RAND_MAX);
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

extern void __attribute__((weak)) (*__fini_array_start[])();
extern void __attribute__((weak)) (*__fini_array_end[])();
extern void __attribute__((weak)) _fini();

extern __attribute__((visibility("hidden"))) char __is_rtld;

void exit(long exit_code) {
    static char is_exiting = 0;
    if (__atomic_load_n(&is_exiting, __ATOMIC_ACQUIRE)) {
        // a different thread and/or atexit handler called exit, UB
        _exit(exit_code);
    }
    __atomic_store_n(&is_exiting, 1, __ATOMIC_RELEASE);

    __call_atexit();

    if (__is_rtld)
        _exit(exit_code);

    for (int i = __fini_array_end - __fini_array_start - 1; i >= 0; i--)
        __fini_array_start[i]();

    if (_fini)
        _fini();
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

#include <assert.h>

extern char ** environ;

char * getenv(const char * name) {
    size_t name_len = strchrnul(name, '=') - name;
    if (!name_len || name[name_len])
        return NULL;

    for (int i = 0; environ[i] != NULL; i++) {
        if (strncmp(name, environ[i], name_len) == 0 && environ[i][name_len] == '=') {
            return environ[i] + name_len + 1;
        }
    }
    return NULL;
}
extern __attribute__((visibility("hidden"))) char __is_secure;

char *secure_getenv(const char *name) {
    if (!__is_secure)
        return NULL;
    return getenv(name);
}

// I could either leak everything as in glibc
// or keep track of allocations like in musl
// I chose the musl approach

static void change_env_ptr(const char * old, const char * new) {
    if (!old && !new)
        return;

    static const char ** alloced_ptrs = NULL;
    static size_t alloced_ptrs_size = 0;

    if (!alloced_ptrs || !old) {
        extend:
        const char ** vec = realloc(alloced_ptrs, ++alloced_ptrs_size * sizeof(char*));
        if (!vec)
            return; // gg rip we'll leak this one
        alloced_ptrs = vec;
        alloced_ptrs[alloced_ptrs_size - 1] = new;
        return;
    }

    for (size_t i = 0; i < alloced_ptrs_size; i++) {
        if (alloced_ptrs[i] == old) {
            free((void*)old);
            alloced_ptrs[i] = new;
            return;
        }
        if (!alloced_ptrs[i]) {
            alloced_ptrs[i] = new;
            new = NULL;
        }
    }
    if (new)
        goto extend;
}

int unsetenv(const char *name) {
    if (!name || !*name) {
        ___set_errno(EINVAL);
        return -1;
    }
    size_t name_len = strchrnul(name, '=') - name;
    if (name[name_len]) {
        ___set_errno(EINVAL);
        return -1;
    }

    int shift_start = -1;
    int i = 0;
    for (i = 0; environ[i] != NULL; i++) {
        if (shift_start == -1 && strncmp(name, environ[i], name_len) == 0 && environ[i][name_len] == '=') {
            change_env_ptr(environ[i], NULL);
            shift_start = i;
        }
    }
    if (shift_start == -1)
        return 0;
    memcpy(environ + shift_start, environ + shift_start + 1, (i - shift_start) * sizeof(char*));
    return 0;
}

static int __putenv(char * string, char alloced) {
    static int environ_alloced = 0; // allocated by us

    size_t name_len = strchrnul(string, '=') - string;

    int i = 0;
    for (i = 0; environ[i] != NULL; i++) {
        if (strncmp(string, environ[i], name_len) == 0 && environ[i][name_len] == '=') {
            change_env_ptr(environ[i], alloced ? string : NULL);
            environ[i] = string;
            return 0;
        }
    }

    char ** new_environ = realloc(environ_alloced ? environ : NULL, (i+1) * sizeof(char*));
    if (!new_environ) {
        if (alloced)
            free(string);
        ___set_errno(ENOMEM);
        return -1;
    }
    if (!environ_alloced)
        memcpy(new_environ, environ, i * sizeof(char*));
    environ_alloced = 1;
    new_environ[i] = string;
    environ = new_environ;
    if (alloced)
        change_env_ptr(NULL, string);
    return 0;
}

int putenv(char *string) {
    if (!string || !*string) {
        ___set_errno(EINVAL);
        return -1;
    }
    if (!*strchrnul(string, '='))
        return unsetenv(string);
    return __putenv(string, 0);
}

int setenv(const char *envname, const char *envval, int overwrite) {
    if (!envname || !*envname || *strchrnul(envname, '=')) {
        ___set_errno(EINVAL);
        return -1;
    }

    if (!overwrite && getenv(envname))
        return 0;
    size_t env_len = strlen(envname);
    size_t total = env_len + 1 + 1;
    if (envval)
        total += strlen(envval);

    char * new_var = malloc(total);
    if (!new_var) {
        ___set_errno(ENOMEM);
        return -1;
    }

    strcpy(new_var, envname);
    new_var[env_len] = '=';
    strcpy(new_var + env_len + 1, envval);
    return __putenv(new_var, 1);
}