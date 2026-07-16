#include "include/block/memdisk.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "../libc/src/include/time.h"
#include "../libc/src/include/sys/types.h"
#include "include/kernel.h"
#include "include/mm/kernel_memory.h"
#include "../libc/src/include/errno.h"
#include <stdint.h>


spinlock_t sleep_queue_lock = {0};

struct sleep_queue {
    process_t * process; // record keeping
    thread_t * thread;
    unsigned int magic_queue_value;
    time_t time_delta_usec;
    struct sleep_queue * next;
};

struct sleep_queue * sq = NULL;

static void sleep_pop_thread() {
    if (sq == NULL) return;
    kassert(sq->thread);
    kassert(sq->thread->instances > 0);

    if (sq->magic_queue_value == sq->thread->magic_queue_value) {
        sq->thread->status = SCHED_RUNNABLE;

        // like signals, sleeping (when used internally) can invalidate thread wait queues
        __atomic_add_fetch(&sq->thread->magic_queue_value, 1, __ATOMIC_ACQUIRE);
    }
    if (__atomic_sub_fetch(&sq->thread->instances, 1, __ATOMIC_RELEASE) == 0) kfree(sq->thread);

    struct sleep_queue * old = sq;
    sq = sq->next;
    kfree(old);
}

void sleep_remove_thread(process_t * pprocess, thread_t * thread) {
    kassert(thread);
    kassert(thread->instances > 0);
    if (sq == NULL) return;

    spinlock_acquire(&sleep_queue_lock);
    if (sq->thread == thread && sq->process == pprocess) {
        if (__atomic_sub_fetch(&thread->instances, 1, __ATOMIC_RELEASE) == 0) kfree(thread);

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
            if (__atomic_sub_fetch(&thread->instances, 1, __ATOMIC_RELEASE) == 0) kfree(thread);

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

void sleep_sched_tick(size_t ticks) {
    if (sq == NULL) return;
    spinlock_acquire(&sleep_queue_lock);

    size_t delta = RTC_TIME_RESOLUTION_USEC * ticks;

    while (sq && sq->time_delta_usec <= delta) {
        delta -= sq->time_delta_usec;
        sleep_pop_thread();
    }
    if (sq)
        sq->time_delta_usec -= delta;

    spinlock_release(&sleep_queue_lock);
}

long sys_nanosleep(process_t * pprocess, thread_t * thread, struct timespec requested, struct timespec * elapsed) {
    kassert(thread);
    kassert(thread->instances > 0);

    if (requested.tv_sec < 0) return 0;

    if (requested.tv_nsec >= 1000000000 || requested.tv_nsec < 0) return -EINVAL;

    time_t requested_usec = requested.tv_sec * 1000000 + requested.tv_nsec/1000;
    time_t delta_usec = requested_usec;
    if (requested_usec == 0) return 0;

    time_t old_time_usec = uptime_clicks * RTC_TIME_RESOLUTION_USEC;

    struct sleep_queue *new_entry = kalloc(sizeof(struct sleep_queue));
    kassert(new_entry);
    *new_entry = (struct sleep_queue) {
        .process = pprocess,
        .thread = thread,
        .magic_queue_value = thread->magic_queue_value,
    };
    if (__atomic_add_fetch(&thread->instances, 1, __ATOMIC_RELEASE) == UINT32_MAX) panic("Overflown thread instance count!");

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

    if (old_time_usec + requested_usec > uptime_clicks * RTC_TIME_RESOLUTION_USEC) {
        sleep_remove_thread(pprocess, thread);

        if (elapsed == NULL) return -EINTR;

        requested_usec -= uptime_clicks * RTC_TIME_RESOLUTION_USEC - old_time_usec;

        *elapsed = (struct timespec) {
            .tv_nsec = (requested_usec % 1000000)*1000,
            .tv_sec = requested_usec / 1000000
        };
        return -EINTR;
    }
    return 0;
}

unsigned sys_alarm(unsigned seconds) {
    unsigned int prev_secs = 0;

    if (current_process->next_alarm) {
        time_t old_alarm = uptime_clicks - current_process->next_alarm;
        prev_secs = (old_alarm + RTC_TIMER_RESOLUTION_HZ - 1) / RTC_TIMER_RESOLUTION_HZ;
    }
    if (seconds == 0)
        current_process->next_alarm = 0;
    else
        current_process->next_alarm = uptime_clicks + seconds * RTC_TIMER_RESOLUTION_HZ;

    return prev_secs;
}