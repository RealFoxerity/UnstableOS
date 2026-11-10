#include <stdio.h>
#include <stdio_ext.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <assert.h>
#include <stdlib.h>

#define REMAIN(tq, max_size) (\
    (tq)->head <= (tq)->tail ? \
        ((tq)->tail - (tq)->head) : \
        ((max_size) - (tq)->head + (tq)->tail)\
    )

size_t __fbufsize(FILE *stream) {
    return __atomic_load_n(&stream->buf.size, __ATOMIC_RELAXED);
}
size_t __fpending(FILE *stream) {
    flockfile(stream);
    size_t ret = REMAIN(&stream->buf, stream->buf.size);
    funlockfile(stream);
    return ret;
}
int __flbf(FILE *stream) {
    return __atomic_load_n(&stream->buffered, __ATOMIC_RELAXED) == _IOLBF ? 1 : 0;
}
int __freadable(FILE *stream) {
    return !!(__atomic_load_n(&stream->mode, __ATOMIC_RELAXED) & O_RDONLY);
}
int __fwritable(FILE *stream) {
    return !!(__atomic_load_n(&stream->mode, __ATOMIC_RELAXED) & O_WRONLY);
}
int __freading(FILE *stream) {
    return __atomic_load_n(&stream->current_mode, __ATOMIC_RELAXED) == O_RDONLY;
}
int __fwriting(FILE *stream) {
    return __atomic_load_n(&stream->current_mode, __ATOMIC_RELAXED) == O_WRONLY;
}
//int __fsetlocking(FILE *stream, int type);
extern FILE * __files;
extern pthread_mutex_t __files_lock;
void _flushlbf() {
    assert(!pthread_mutex_lock(&__files_lock));
    FILE * f = __files;
    while (f) {
        if (f->buffered == _IOLBF)
            fflush(f);
        f = f->next;
    }
    pthread_mutex_unlock(&__files_lock);
}


int fpurge(FILE *stream) {
    if (stream == NULL || stream->pure_buf) {
        ___set_errno(EBADF);
        return -1;
    }
    flockfile(stream);
    stream->buf.head = stream->buf.tail;
    stream->ungetc_buf.head = stream->ungetc_buf.tail;
    funlockfile(stream);
    return 0;
}

void __fpurge(FILE *stream) {
    fpurge(stream);
}

void __fseterr(FILE *stream) {
    stream->was_error = 1;
}