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

int sys_pipe(int fildes[2]) {
    struct pipe * new_pipe = kalloc(sizeof(struct pipe));
    if (new_pipe == NULL) return -ENOMEM;
    memset(new_pipe, 0, sizeof(struct pipe));
    new_pipe->readers = 1;

    spinlock_acquire(&kernel_inode_lock);

    inode_t * pipe_inode = get_free_inode();
    kassert(pipe_inode);

    pipe_inode->instances ++;
    pipe_inode->mode = S_IFFIFO;

    pipe_inode->pipe = new_pipe;

    const int fd1 = get_fd_from_inode(pipe_inode, O_RDONLY);
    if (fd1 == -1) {
        kfree(new_pipe);
        spinlock_release(&kernel_inode_lock);
        return fd1;
    }

    const int fd2 = get_fd_from_inode(pipe_inode, O_WRONLY);
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

static int pipe_put_ch(struct pipe * pq, const unsigned char c) {
    kassert(pq->head < PIPE_BUF && pq->tail < PIPE_BUF);
    spinlock_acquire_interruptible(&pq->pipe_lock);
    while (FULL(pq)) {
        spinlock_release(&pq->pipe_lock);
        thread_queue_unblock(&pq->read_queue); // force reading, below release so we don't waste a timeslice

        thread_queue_add(&pq->write_queue, current_process, current_thread, SCHED_INTERR_SLEEP);

        if (pq->readers < 1) {
            thread_queue_unblock_all(&pq->write_queue); // force SIGPIPE to all
            signal_process(current_process, &(siginfo_t) {.si_signo = SIGPIPE});
            return 256;
        }
        if (current_thread->sa_to_be_handled) {
            return 256;
        }

        spinlock_acquire_interruptible(&pq->pipe_lock);
    }
    pq->pipe_fifo[pq->tail] = c;
    INC(pq);
    spinlock_release(&pq->pipe_lock);
    return 0;
}

ssize_t pipe_write(const file_descriptor_t * file, const void * s, size_t n) {
    if (n == 0) return 0;
    kassert(file);
    kassert(file->inode);
    kassert(S_ISFIFO(file->inode->mode));
    kassert(file->inode->pipe);
    kassert(s);

    if (file->inode->pipe->readers < 1) {
        thread_queue_unblock_all(&file->inode->pipe->write_queue); // force SIGPIPE to all
        signal_process(current_process, &(siginfo_t) {.si_signo = SIGPIPE});
        return -EPIPE;
    }

    for (size_t i = 0; i < n; i++) {
        if (pipe_put_ch(file->inode->pipe, ((unsigned char*)s)[i]) == 256) {
            // outside of pipe_put_ch so that we don't needlessly reschedule over and over again for a single char
            thread_queue_unblock(&file->inode->pipe->read_queue);
            return i == 0 ? -EINTR : i;
        }
    }
    thread_queue_unblock(&file->inode->pipe->read_queue);
    return n;
}

static int pipe_get_ch(struct pipe * pq) {
    kassert(pq->head < PIPE_BUF && pq->tail < PIPE_BUF);

    again:
    if (EMPTY(pq))
        thread_queue_add(&pq->read_queue, current_process, current_thread, SCHED_INTERR_SLEEP);
    if (current_thread->sa_to_be_handled) return 256;

    unsigned char out = 0;
    spinlock_acquire_interruptible(&pq->pipe_lock);
    if (EMPTY(pq)) {
        spinlock_release(&pq->pipe_lock);
        thread_queue_unblock(&pq->write_queue); // force writing
        goto again;
    }

    out = pq->pipe_fifo[pq->head];
    DEC(pq);
    spinlock_release(&pq->pipe_lock);
    return out;
}

ssize_t pipe_read(const file_descriptor_t * file, void * s, size_t n) {
    if (n == 0) return 0;
    if (n > SSIZE_MAX) { return -E2BIG; }
    kassert(file);
    kassert(file->inode);
    kassert(S_ISFIFO(file->inode->mode));
    kassert(file->inode->pipe);
    kassert(s);

    if (file->inode->instances <= file->inode->pipe->readers) {
#ifdef SIGPIPE_ON_READ
        signal_process(current_process, &(siginfo_t) {.si_signo = SIGPIPE});
#endif
        return -EPIPE;
    }

    for (unsigned char * i = s; i < (unsigned char*)s + n; i++) {
        int out = pipe_get_ch(file->inode->pipe);
        if (out == 256) {
            thread_queue_unblock(&file->inode->pipe->write_queue);

            if (i - (unsigned char *)s == 0) return -EINTR;
            return i - (unsigned char *)s;
        }
        *i = out;
    }
    // see comment in pipe_write
    thread_queue_unblock(&file->inode->pipe->write_queue);
    return n;
}
