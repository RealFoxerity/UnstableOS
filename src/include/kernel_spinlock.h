#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#define SPINLOCK_UNLOCKED 0
#define SPINLOCK_LOCKED 1

struct {
    unsigned long state; // has to be the width of a single register for cmpxchg
} typedef spinlock_t;

void spinlock_waiton(spinlock_t * lock);
void spinlock_acquire(spinlock_t * lock);
void spinlock_release(spinlock_t * lock);
#endif