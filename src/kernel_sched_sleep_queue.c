#include "include/block/memdisk.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "../libc/src/include/time.h"
#include "../libc/src/include/sys/types.h"
#include "include/kernel.h"
#include "include/mm/kernel_memory.h"
#include "include/errno.h"
#include <stdint.h>


spinlock_t sleep_queue_lock = {0};

struct sleep_queue {
    process_t * process; // record keeping
    thread_t * thread;
    time_t time_delta_usec;
    struct sleep_queue * next;
};

struct sleep_queue * sq = NULL;

static void sleep_pop_thread() {
    if (sq == NULL) return;
    kassert(sq->thread);
    kassert(sq->thread->instances > 0);

    sq->thread->status = SCHED_RUNNABLE;

    if (__atomic_sub_fetch(&sq->thread->instances, 1, __ATOMIC_RELAXED) == 0) kfree(sq->thread);

    struct sleep_queue * old = sq;
    sq = sq->next;
    kfree(old);
}

static void sleep_remove_thread(process_t * pprocess, thread_t * thread) {
    kassert(thread);
    kassert(thread->instances > 0);
    if (sq == NULL) return;

    spinlock_acquire(&sleep_queue_lock);
    if (sq->thread == thread && sq->process == pprocess) {
        if (__atomic_sub_fetch(&thread->instances, 1, __ATOMIC_RELAXED) == 0) kfree(thread);

        struct sleep_queue * old_sq = sq;
        if (sq->next != NULL) {
            sq->next->time_delta_usec += sq->time_delta_usec;
        } else {
            sq = NULL;
        }

        kfree(old_sq);
        spinlock_release(&sleep_queue_lock);
        return;
    }
    struct sleep_queue * prev = sq, * curr = sq->next;

    while (curr != NULL) {
        if (curr->thread == thread && curr->process == pprocess) {
            if (__atomic_sub_fetch(&thread->instances, 1, __ATOMIC_RELAXED) == 0) kfree(thread);

            prev->next = curr->next;

            if (curr->next != NULL) {
                curr->next->time_delta_usec += curr->time_delta_usec;
            }

            kfree(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    spinlock_release(&sleep_queue_lock);
}

void sleep_sched_tick() {
    if (sq == NULL) return;
    spinlock_acquire(&sleep_queue_lock);

    if (sq->time_delta_usec <= RTC_TIME_RESOLUTION_USEC) {
        sleep_pop_thread();
    } else
        __atomic_sub_fetch(&sq->time_delta_usec, RTC_TIME_RESOLUTION_USEC, __ATOMIC_RELAXED);

    spinlock_release(&sleep_queue_lock);
}

ssize_t sys_nanosleep(process_t * pprocess, thread_t * thread, struct timespec requested, struct timespec * elapsed) {
    kassert(thread);
    kassert(thread->instances > 0);

    if (requested.tv_sec < 0) return 0;

    if (requested.tv_nsec >= 1000000000 || requested.tv_nsec < 0) return EINVAL;

    time_t requested_usec = requested.tv_sec * 1000000 + requested.tv_nsec/1000;
    time_t delta_usec = requested_usec;
    if (requested_usec == 0) return 0;

    time_t old_time_usec = uptime_clicks * RTC_TIME_RESOLUTION_USEC;

    struct sleep_queue *new_entry = kalloc(sizeof(struct sleep_queue));
    kassert(new_entry);
    *new_entry = (struct sleep_queue) {
        .process = pprocess,
        .thread = thread,
    };
    if (__atomic_add_fetch(&thread->instances, 1, __ATOMIC_RELAXED) == UINT32_MAX) panic("Overflown thread instance count!");

    spinlock_acquire(&sleep_queue_lock);

    struct sleep_queue * curr = NULL, * next = sq;

    time_t queue_deltas_usec = 0;
    while (next != NULL) {
        if (queue_deltas_usec + next->time_delta_usec > requested_usec) break;
        queue_deltas_usec += next->time_delta_usec;
        curr = next;
        next = next->next;
    }
    delta_usec -= queue_deltas_usec;
    new_entry->time_delta_usec = delta_usec;
    new_entry->next = next;

    if (curr == NULL) {
        if (sq != NULL) {
            sq->time_delta_usec -= delta_usec;
        }
        sq = new_entry;
    } else {
        curr->next = new_entry;
        if (next != NULL) {
            next->time_delta_usec -= delta_usec;
        }
    }

    current_thread->status = SCHED_INTERR_SLEEP;

    spinlock_release(&sleep_queue_lock);
    reschedule();

    if (old_time_usec + requested_usec + RTC_TIME_RESOLUTION_USEC < uptime_clicks * RTC_TIME_RESOLUTION_USEC) {
        sleep_remove_thread(pprocess, thread);

        if (elapsed == NULL) return EINTR;

        requested_usec -= uptime_clicks * RTC_TIME_RESOLUTION_USEC - old_time_usec;

        *elapsed = (struct timespec) {
            .tv_nsec = (requested_usec % 1000000)*1000,
            .tv_sec = requested_usec / 1000000
        };
        return EINTR;
    }
    return 0;
}