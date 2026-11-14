//void kernel_sem_init();
//void kernel_sem_destroy();
#include "kernel.h"
#include "kernel_interrupts.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include <string.h>

#define kprintf(fmt, ...) kprintf("Kernel Sync: "fmt, ##__VA_ARGS__)

void spinlock_acquire(spinlock_t * lock) { // disables interrupts after lock
    if (!lock) panic("Tried to lock a NULL spinlock");
    CRIT_SEC_START

    asm volatile ("pushf; pop %0;" : "=R"(lock->eflags));

    // current process being null is an easy way to tell if the scheduler is initialized
    // before scheduler, we don't have interrupts and enabling them wouldn't be good
    if (!current_process) {
        lock->state = SPINLOCK_LOCKED;
        return;
    }

    asm volatile("cli");
    if (__atomic_compare_exchange_n(&lock->state, &(unsigned long){SPINLOCK_UNLOCKED}, SPINLOCK_LOCKED, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return; // in case we don't even need the sti

    do {
        //asm volatile ("pause");
        reschedule();
    } while (lock->state != SPINLOCK_UNLOCKED || !__atomic_compare_exchange_n(&lock->state, &(unsigned long){SPINLOCK_UNLOCKED}, SPINLOCK_LOCKED, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
}

// same as normal spinlock_acquire, but doesn't call cli
void spinlock_acquire_interruptible(spinlock_t * lock) {
    spinlock_acquire(lock);
    asm volatile("sti;");
}

// WARNING NO WAY TO DETECT DEADLOCKS FOR SPINLOCKING
// NEVER TRY TO LOCK A GLOBAL SPINLOCK INSIDE NONREENTRANT INTERRUPTS
void spinlock_acquire_nonreentrant(spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL spinlock");
    CRIT_SEC_START

    asm volatile ("pushf; pop %0;" : "=R"(lock->eflags));

    if (!current_process) {
        lock->state = SPINLOCK_LOCKED;
        return;
    }

    do {
        asm volatile ("pause");
    } while (!__atomic_compare_exchange_n(&lock->state, &(unsigned long){SPINLOCK_UNLOCKED}, SPINLOCK_LOCKED, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
}

void spinlock_release(spinlock_t * lock) {if (!lock) panic("Tried to release NULL spinlock"); CRIT_SEC_END __atomic_store_n(&lock->state, SPINLOCK_UNLOCKED, __ATOMIC_RELEASE); asm volatile ("push %0; popf;" :: "R"(lock->eflags));}


// maybe we don't need the vlock?

void rw_spinlock_acquire_read(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    spinlock_acquire_interruptible(&lock->vlock);

    if (__atomic_add_fetch(&lock->value, 1, __ATOMIC_ACQUIRE) == 1) {
        spinlock_acquire_interruptible(&lock->wlock);
        CRIT_SEC_END
    }

    spinlock_release(&lock->vlock);
}

void rw_spinlock_release_read(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    spinlock_acquire_interruptible(&lock->vlock);

    if (lock->value == 0)
        panic("Tried to release a read rw spinlock with 0 instances");

    if (__atomic_sub_fetch(&lock->value, 1, __ATOMIC_RELEASE) == 0) {
        CRIT_SEC_START
        spinlock_release(&lock->wlock);
    }

    spinlock_release(&lock->vlock);
}

void rw_spinlock_acquire_write(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    spinlock_acquire_interruptible(&lock->wlock);
}

void rw_spinlock_release_write(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    spinlock_release(&lock->wlock);
}

void rw_spinlock_downgrade(rw_spinlock_t * lock) {
    if (!lock) panic("Tried to lock a NULL rw spinlock");
    __atomic_add_fetch(&lock->value, 1, __ATOMIC_ACQUIRE);
    CRIT_SEC_END
}

void semaphore_post(semaphore_t * sem) {
    if (!sem) panic("Tried to post a NULL semaphore");
    __atomic_add_fetch(&sem->value, 1, __ATOMIC_RELEASE);
    CRIT_SEC_END
}
void semaphore_wait(semaphore_t * sem) {
    if (!sem) panic("Tried to wait a NULL semaphore");
    unsigned long v;
    do {
        v = __atomic_load_n(&sem->value, __ATOMIC_ACQUIRE);
        if (v == 0) reschedule();
    } while (!v || !__atomic_compare_exchange_n(&sem->value, &v, v-1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
    CRIT_SEC_START
}