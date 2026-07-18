#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#define SPINLOCK_UNLOCKED 0
#define SPINLOCK_LOCKED 1

struct {
    unsigned long state; // has to be the width of a single register for cmpxchg
    unsigned long eflags; // so that spinlock_acquire can disable interrupts and spinlock_release can reenable them
} typedef spinlock_t;

void spinlock_acquire(spinlock_t * lock); // reschedules instead of busy loop checking, automatically disables interrupts when spinlock locked
void spinlock_acquire_interruptible(spinlock_t * lock); // same as spinlock_acquire, but doesn't disable interrupts
void spinlock_acquire_nonreentrant(spinlock_t * lock); // busy loop, also disables interrupts
void spinlock_release(spinlock_t * lock); // reenables interrupts if they were enabled when acquiring the lock


struct {
    unsigned long value;
    spinlock_t    vlock;
    spinlock_t    wlock;
} typedef rw_spinlock_t;
// rw locks are always interruptible (calling spinlock_acquire_interruptible)
void rw_spinlock_acquire_read (rw_spinlock_t * lock);
void rw_spinlock_release_read (rw_spinlock_t * lock);
void rw_spinlock_acquire_write(rw_spinlock_t * lock);
void rw_spinlock_release_write(rw_spinlock_t * lock);

// atomically downgrades from write to read lock, use release read after
void rw_spinlock_downgrade(rw_spinlock_t * lock);
#endif