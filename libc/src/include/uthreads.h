#ifndef UTHREADS_H
#define UTHREADS_H

typedef long semaphore_t;
typedef long mutex_t;

semaphore_t semaphore_init(int initial_value);
void semaphore_post(semaphore_t semaphore_id);
void semaphore_wait(semaphore_t semaphore_id);
void semaphore_destroy(semaphore_t semaphore_id);

mutex_t mutex_init();
void mutex_lock(mutex_t mutex_id);
void mutex_unlock(mutex_t mutex_id);
void mutex_destroy(mutex_t mutex_id);
#endif