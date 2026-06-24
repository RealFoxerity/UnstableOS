#include <uthreads.h>
#include <unistd.h>
#include <errno.h>
#include <UnstableOS/syscalls.h>

semaphore_t semaphore_init(int initial_value) {
    semaphore_t ret = syscall(SYSCALL_SEM_INIT, initial_value);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}

void semaphore_post(semaphore_t semaphore_id) {
    syscall(SYSCALL_SEM_POST, semaphore_id);
}

void semaphore_wait(semaphore_t semaphore_id) {
    syscall(SYSCALL_SEM_WAIT, semaphore_id);
}

void semaphore_destroy(semaphore_t semaphore_id) {
    syscall(SYSCALL_SEM_DESTROY, semaphore_id);
}

mutex_t mutex_init() {
    return semaphore_init(1);
}

void mutex_lock(mutex_t mutex_id) {
    semaphore_wait(mutex_id);
}

void mutex_unlock(mutex_t mutex_id) {
    semaphore_post(mutex_id);
}

void mutex_destroy(mutex_t mutex_id) {
    semaphore_destroy(mutex_id);
}
