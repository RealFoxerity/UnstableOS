#include "fs/fs.h"
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_sched.h"
#include <string.h>
#include <sys/types.h>
#include <errno.h>

// the same defines as in kernel_tty_io.h because it's the same idea
// TODO: low priority, maybe refactor ttys to use pipes?

#define EMPTY(pipe) ((pipe)->head == (pipe)->tail)
#define FULL(pipe) (((pipe)->head == 0 && (pipe)->tail == PIPE_BUF - 1) || (pipe)->tail == (pipe)->head - 1)
#define REMAIN(pipe) (                              \
    (pipe)->head <= (pipe)->tail ?                  \
        ((pipe)->tail - (pipe)->head) :             \
        (PIPE_BUF - (pipe)->head + (pipe)->tail)    \
    )// how many elements still in queue
#define INC(pipe) ((pipe)->tail = ((pipe)->tail+1)%PIPE_BUF) // lengthens queue
#define DEC(pipe) ((pipe)->head = ((pipe)->head+1)%PIPE_BUF) // shortens queue by removing oldest element

int sys_pipe(int fildes[2], int flags) {
    flags &= O_CLOEXEC | O_CLOFORK;

    struct pipe * new_pipe = kalloc(sizeof(struct pipe));
    if (new_pipe == NULL) return -ENOMEM;
    memset(new_pipe, 0, sizeof(struct pipe));
    new_pipe->readers = 1;
    new_pipe->writers = 1;

    spinlock_acquire(&kernel_inode_lock);

    inode_t * pipe_inode = get_free_inode();
    kassert(pipe_inode);

    pipe_inode->instances ++;
    pipe_inode->mode = S_IFIFO;

    pipe_inode->pipe = new_pipe;

    const int fd1 = get_fd_from_inode(pipe_inode, O_RDONLY | flags);
    if (fd1 == -1) {
        kfree(new_pipe);
        spinlock_release(&kernel_inode_lock);
        return fd1;
    }

    const int fd2 = get_fd_from_inode(pipe_inode, O_WRONLY | flags);
    if (fd2 == -1) {
        sys_close(fd1);
        kfree(new_pipe);
        spinlock_release(&kernel_inode_lock);
        return fd2;
    }
    spinlock_release(&kernel_inode_lock);

    fildes[0] = fd1;
    fildes[1] = fd2;
    return 0;
}

int open_pipe(inode_t * pipe, int flags) {
    if (!(flags & O_RDWR))
        return 0;
    kassert(pipe);
    kassert(S_ISFIFO(pipe->mode));
    spinlock_acquire(&pipe->lock);
    if (!pipe->pipe) {
        struct pipe * new_pipe = kalloc(sizeof(struct pipe));
        if (!new_pipe) {
            spinlock_release(&pipe->lock);
            return -ENOMEM;
        }
        memset(new_pipe, 0, sizeof(struct pipe));
        new_pipe->named_wait = 1;
        pipe->pipe = new_pipe;
    }
    spinlock_release(&pipe->lock);

    spinlock_acquire(&pipe->pipe->pipe_lock);
    if ((flags & O_RDONLY && __atomic_load_n(&pipe->pipe->readers, __ATOMIC_ACQUIRE) == SIZE_MAX) ||
        (flags & O_WRONLY && __atomic_load_n(&pipe->pipe->writers, __ATOMIC_ACQUIRE) == SIZE_MAX)) {
        spinlock_release(&pipe->pipe->pipe_lock);
        return -EBUSY;
    }
    if (flags & O_RDONLY)
        __atomic_add_fetch(&pipe->pipe->readers, 1, __ATOMIC_ACQUIRE);
    if (flags & O_WRONLY)
        __atomic_add_fetch(&pipe->pipe->writers, 1, __ATOMIC_ACQUIRE);
    if (pipe->pipe->readers > 0 && pipe->pipe->writers > 0) {
        __atomic_store_n(&pipe->pipe->named_wait, 0, __ATOMIC_RELEASE);
        thread_queue_unblock_all_nonreentrant(&pipe->pipe->read_queue);
        thread_queue_unblock_all_nonreentrant(&pipe->pipe->write_queue);
    }
    spinlock_release(&pipe->pipe->pipe_lock);
    return 0;
}

static int pipe_put_ch(struct pipe * pq, const unsigned char c) {
    kassert(pq->head < PIPE_BUF && pq->tail < PIPE_BUF);
    while (pq->named_wait) {
        if (check_eintr()) return 256;
        asm volatile("cli");
        thread_queue_add_nonreentrant(&pq->write_queue, current_process, current_thread);
        current_thread->status = SCHED_INTERR_SLEEP;
        spinlock_release(&pq->pipe_lock);
        reschedule();
        spinlock_acquire_interruptible(&pq->pipe_lock);
    }
    // paranoid checking to avoid all kinds of races
    if (__atomic_load_n(&pq->readers, __ATOMIC_ACQUIRE) == 0) {
        thread_queue_unblock_all(&pq->write_queue); // force SIGPIPE to all
        signal_process(current_process, &(siginfo_t) {.si_signo = SIGPIPE});
        return -2;
    }
    while (FULL(pq)) {
        spinlock_release(&pq->pipe_lock);
        asm volatile("cli"); // avoid thread queue races, TODO: change when adding atomic queues

        thread_queue_add_nonreentrant(&pq->write_queue, current_process, current_thread);
        current_thread->status = SCHED_INTERR_SLEEP;
        thread_queue_unblock_nonreentrant(&pq->read_queue); // force reading, below release so we don't waste a timeslice
        asm volatile("sti");
        reschedule();

        spinlock_acquire_interruptible(&pq->pipe_lock);
        if (__atomic_load_n(&pq->readers, __ATOMIC_ACQUIRE) == 0) {
            thread_queue_unblock_all(&pq->write_queue); // force SIGPIPE to all
            signal_process(current_process, &(siginfo_t) {.si_signo = SIGPIPE});
            return -2;
        }
        if (check_eintr()) {
            return 256;
        }
    }
    pq->pipe_fifo[pq->tail] = c;
    INC(pq);
    return 0;
}

ssize_t pipe_write(const file_descriptor_t * file, const void * s, size_t n) {
    if (n == 0) return 0;
    kassert(file);
    kassert(file->inode);
    kassert(S_ISFIFO(file->inode->mode));
    kassert(file->inode->pipe);
    kassert(s);

    spinlock_acquire_interruptible(&file->inode->pipe->pipe_lock);
    for (size_t i = 0; i < n; i++) {
        int out = pipe_put_ch(file->inode->pipe, ((unsigned char*)s)[i]);
        if (out == 256) {
            // outside of pipe_put_ch so that we don't needlessly reschedule over and over again for a single char
            spinlock_release(&file->inode->pipe->pipe_lock);
            thread_queue_unblock(&file->inode->pipe->read_queue);
            return i == 0 ? -EINTR : i;
        }
        if (out == -2) {
            spinlock_release(&file->inode->pipe->pipe_lock);
            return -EPIPE;
        }
    }
    spinlock_release(&file->inode->pipe->pipe_lock);
    thread_queue_unblock(&file->inode->pipe->read_queue);
    return n;
}

static int pipe_get_ch(struct pipe * pq) {
    kassert(pq->head < PIPE_BUF && pq->tail < PIPE_BUF);

    unsigned char out = 0;
    while (pq->named_wait) {
        if (check_eintr()) return 256;
        asm volatile("cli");
        thread_queue_add_nonreentrant(&pq->read_queue, current_process, current_thread);
        current_thread->status = SCHED_INTERR_SLEEP;
        spinlock_release(&pq->pipe_lock);
        reschedule();
        spinlock_acquire_interruptible(&pq->pipe_lock);
    }
    if (EMPTY(pq)) {
        if (__atomic_load_n(&pq->writers, __ATOMIC_ACQUIRE) == 0) return -2;
        return -1;
    }

    out = pq->pipe_fifo[pq->head];
    DEC(pq);
    return out;
}

ssize_t pipe_read(const file_descriptor_t * file, void * s, size_t n) {
    if (n == 0) return 0;
#ifdef E2BIG_ON_2G
    if (n > SSIZE_MAX) return -E2BIG;
#else
    if (n > SSIZE_MAX) n = SSIZE_MAX;
#endif
    kassert(file);
    kassert(file->inode);
    kassert(S_ISFIFO(file->inode->mode));
    kassert(file->inode->pipe);
    kassert(s);

    // ideally we want to drain the buffer to 0 before quitting
    // this check is then again in pipe_get_ch in the EMPTY branch
    //if (file->inode->pipe->writers == 0) return 0;

    spinlock_acquire_interruptible(&file->inode->pipe->pipe_lock);
    for (unsigned char * i = s; i < (unsigned char*)s + n; i++) {
        int out = pipe_get_ch(file->inode->pipe);
        if (out == 256) {
            thread_queue_unblock(&file->inode->pipe->write_queue);

            spinlock_release(&file->inode->pipe->pipe_lock);
            if (i - (unsigned char *)s == 0) return -EINTR;
            return i - (unsigned char *)s;
        }
        if (out == -1) {
            if (i - (unsigned char *)s == 0) {
                // pipes are by default unbuffered
                // that would be highly inefficient so we build up a slight buffering system
                // pipe reading should however block until any byte is received

                thread_queue_unblock_nonreentrant(&file->inode->pipe->write_queue);
                asm volatile ("cli");
                thread_queue_add_nonreentrant(&file->inode->pipe->read_queue, current_process, current_thread);
                current_thread->status = SCHED_INTERR_SLEEP;
                spinlock_release(&file->inode->pipe->pipe_lock);
                reschedule();
                spinlock_acquire_interruptible(&file->inode->pipe->pipe_lock);

                i--;
                continue;
            }
            spinlock_release(&file->inode->pipe->pipe_lock);
            return i - (unsigned char *)s;
        }
        if (out == -2) {
            spinlock_release(&file->inode->pipe->pipe_lock);
            return i - (unsigned char *)s; // also handles EOF
        }
        *i = out;
    }
    // see comment in pipe_write
    spinlock_release(&file->inode->pipe->pipe_lock);
    thread_queue_unblock(&file->inode->pipe->write_queue);
    return n;
}
