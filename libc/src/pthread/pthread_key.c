#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "stdio.h"

static void (* volatile key_dtors[PTHREAD_KEYS_MAX])(void *) = {0};

/* Note: POSIX says
 *      Upon key creation, the value NULL shall be associated with the new key in all active threads.
 *      Upon thread creation, the value NULL shall be associated with all defined keys in the new thread.
 * The second point is guaranteed by us memsetting all possible key slots
 * Assuming normal usage, our memset of the entire possible list of keys guarantees the first point for new keys
 * However, threads doing pthread_key_delete() free the spot,
 *  so it's possible for newly created keys to have nonnull values in old threads
 * This would be (and still kinda is) bad, if not for this in pthread_key_delete():
 *       Any attempt to use key following the call to pthread_key_delete() results in undefined behavior.
 * The alternative is to link all threads together and go through them one by one,
 *  which is somewhat bug prone and potentially broken with async cancellation
 * Solution used here is to go sequentially up before wrapping,
 *  with the idea that hopefully we won't run into threads using stale keys before the wrap
 */
#include <stdlib.h>
#include <assert.h>
__attribute__((constructor(2))) static void __pthread_key_init() {
    pthread_self()->__pthread_keys = malloc(PTHREAD_KEYS_MAX * sizeof(void*));
    assert(pthread_self()->__pthread_keys);
}

// add destructor(10) if you want it to be called on regular exit()
// visibility to not be linkable, because we need this symbol for pthread_exit
__attribute__((visibility("internal"))) void __pthread_key_call_dtors() {
    if (!pthread_self()->__pthread_keys)
        return;

    void ** keys = pthread_self()->__pthread_keys;
    for (int i = 0; i < PTHREAD_DESTRUCTOR_ITERATIONS; i++) {
        char run_again = 0;
        for (int j = 0; j < PTHREAD_KEYS_MAX; j++) {
            void (*destructor)(void *) = __atomic_load_n(&key_dtors[j], __ATOMIC_ACQUIRE);
            if (!destructor || destructor == (void *)-1)
                keys[j] = NULL;
            if (keys[j] == NULL)
                continue;
            void * val = keys[j];
            keys[j] = NULL;
            destructor(val);
            run_again = 1;
        }
        if (!run_again)
            return;
    }
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
    static volatile unsigned long last_idx = 0;
    if (!key)
        return EINVAL;
    if (destructor == NULL) // we want to use NULL as the "not taken" value
        destructor = (void (*)(void*))-1;

    for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
        unsigned long idx = last_idx++;  // intentionally not atomic so that we don't wrap too soon

        if (__atomic_compare_exchange_n(
            &key_dtors[idx % PTHREAD_KEYS_MAX],
            &(void (*)(void *)){NULL}, destructor,
            0,
            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            *key = idx;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key > PTHREAD_KEYS_MAX)
        return EINVAL;
    __atomic_store_n(&key_dtors[key], 0, __ATOMIC_RELEASE);
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key > PTHREAD_KEYS_MAX || !pthread_self()->__pthread_keys)
        return NULL;

    return pthread_self()->__pthread_keys[key];
}
int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key > PTHREAD_KEYS_MAX ||
        !pthread_self()->__pthread_keys ||
        !__atomic_load_n(&key_dtors[key], __ATOMIC_ACQUIRE))
        return EINVAL;

    pthread_self()->__pthread_keys[key] = (void*)value;
    return 0;
}