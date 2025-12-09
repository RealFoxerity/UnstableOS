#ifndef KERNEL_SEMAPHORE_H
#define KERNEL_SEMAPHORE_H

#include "kernel_sched.h"

// not recommended to call these directly to acquire a semaphore for the kernel
//void kernel_sem_init();
//void kernel_sem_destroy();
void kernel_sem_post(process_t * calling_process, int sem_idx);
void kernel_sem_wait(process_t * calling_process, thread_t * calling_thread, int sem_idx);
#endif