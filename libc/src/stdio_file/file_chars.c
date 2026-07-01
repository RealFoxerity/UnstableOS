#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include <string.h>
#include <assert.h>

#define EMPTY(tq) ((tq)->head == (tq)->tail)
#define FULL(tq, max_size) (((tq)->head == 0 && (tq)->tail == (max_size) - 1) || (tq)->tail == (tq)->head - 1)
#define REMAIN(tq, max_size) (\
    (tq)->head <= (tq)->tail ? \
        ((tq)->tail - (tq)->head) : \
        ((max_size) - (tq)->head + (tq)->tail)\
    )// how many elements still in queue
#define INC(tq, max_size) ((tq)->tail = ((tq)->tail+1)%(max_size))
#define DEC(tq, max_size) ((tq)->head = ((tq)->head+1)%(max_size))
#define DEC_LAST(tq, max_size) ((tq)->tail = ((max_size) + (tq)->tail-1)%(max_size))

static unsigned char file_get_ungetc(FILE * stream) {
    DEC_LAST(&stream->ungetc_buf, FILE_UNGETC_ARR_SIZE);
    stream->current_offset ++;
    return stream->ungetc_buf.buf[stream->ungetc_buf.tail];
}

static unsigned char file_get_buffer(FILE * stream) {
    unsigned char c = stream->buf.buf[stream->buf.head];
    DEC(&stream->buf, stream->buf.size);
    stream->current_offset ++;
    return c;
}

static char file_refill_cache(FILE * stream) {
    stream->buf.head = stream->buf.tail = 0;
    ssize_t ret = read(stream->fd, stream->buf.buf, stream->buf.size);

    if (ret == 0)
        stream->was_eof = 1;
    else if (ret < 0) {
        stream->was_error = 1;
        ret = 0;
    }
    else
        stream->buf.tail = ret;

    return !ret;
}

static inline int file_put_buffer(FILE * stream, unsigned char c) {
    stream->current_mode = O_WRONLY; // fflush resets this
    if (FULL(&stream->buf, stream->buf.size))
        if (fflush(stream) != 0)
            return EOF;
    stream->buf.buf[stream->buf.tail] = c;
    INC(&stream->buf, stream->buf.size);
    stream->current_offset ++;
    if (stream->buffered == _IOLBF && c == '\n')
        if (fflush(stream) != 0)
            return EOF;
    return c;
}

int fgetc(FILE *stream) {
    if (!(stream->mode & O_RDONLY)) {
        ___set_errno(EBADF);
        return EOF;
    }
    if (stream == NULL) {
        ___set_errno(EBADF);
        return EOF;
    }

    assert(!pthread_mutex_lock(&stream->mutex));
    // it's ub to follow writes directly by reads without fflush() or positioning
    // even though it's the applications' responsibility to call these
    // I decided to call fflush() on their behalf if they don't
    // fflush() return value is not checked because it could be something the application did
    if (stream->current_mode == O_WRONLY)
        fflush(stream);

    stream->current_mode = O_RDONLY;

    int ret = EOF;
    if (!EMPTY(&stream->ungetc_buf))
        ret = file_get_ungetc(stream);
    else if (!EMPTY(&stream->buf)) // fmemopen always has both head and tail 0
        ret = file_get_buffer(stream);
    else {
        if (stream->pure_buf) {
            if (stream->current_offset == stream->buf.size) {
                stream->was_eof = 1;
                goto unlock;
            }
            ret = stream->buf.buf[stream->current_offset++];
            goto unlock;
        }
        if (stream->buffered) {
            if (!file_refill_cache(stream)) {
                ret = file_get_buffer(stream);
            }
        } else {
            unsigned char c;
            ssize_t status = read(stream->fd, &c, 1);
            if (status == 0)
                stream->was_eof = 1;
            else if (status < 0)
                stream->was_error = 1;
            else {
                stream->current_offset ++;
                ret = c;
            }
        }
    }
    unlock:
    pthread_mutex_unlock(&stream->mutex);
    return ret;
}
char *fgets(char *restrict s, int n, FILE *restrict stream) {
    if (!(stream->mode & O_RDONLY)) {
        ___set_errno(EBADF);
        return NULL;
    }
    if (stream == NULL) {
        ___set_errno(EBADF);
        return NULL;
    }
    if (s == NULL || n <= 1) {
        ___set_errno(EINVAL);
        return NULL;
    }

    assert(!pthread_mutex_lock(&stream->mutex));
    int i = 0;
    int c = EOF;
    for (; i < n - 1; i++) {
        if (c == '\n') // new line gets written still
            break;
        c = fgetc(stream);
        if (c == EOF)
            break;
        s[i] = (char)c;
    }
    s[i] = '\0';
    pthread_mutex_unlock(&stream->mutex);
    if (c == EOF && i == 0)
        return NULL;
    return s;
}

int fputc(int c, FILE *stream) {
    if (!(stream->mode & O_WRONLY)) {
        ___set_errno(EBADF);
        return EOF;
    }
    if (stream == NULL) {
        ___set_errno(EBADF);
        return EOF;
    }

    int ret = EOF;

    assert(!pthread_mutex_lock(&stream->mutex));
    // see the comment in fgetc
    if (stream->current_mode == O_RDONLY)
        fflush(stream);

    stream->current_mode = O_WRONLY;

    unsigned char ch = (unsigned char)c;

    if (stream->buffered == _IONBF) {
        if (stream->pure_buf) {
            if (stream->current_offset == stream->buf.size) {
                stream->was_eof = 1;
                ret = EOF;
                goto unlock;
            }
            stream->buf.buf[stream->current_offset++] = ch;
            goto unlock;
        }
        switch (write(stream->fd, &ch, 1)) {
            case -1:
                stream->was_error = 1;
                break;
            case 0:
                stream->was_eof = 1;
                break;
            default:
                stream->current_offset ++;
                ret = ch;
        }
    } else
        ret = file_put_buffer(stream, ch);
    unlock:
    pthread_mutex_unlock(&stream->mutex);
    return ret;
}
int fputs(const char *restrict s, FILE *restrict stream) {
    if (!(stream->mode & O_WRONLY)) {
        ___set_errno(EBADF);
        return EOF;
    }
    if (stream == NULL) {
        ___set_errno(EBADF);
        return EOF;
    }

    int ret = 0;

    assert(!pthread_mutex_lock(&stream->mutex));
    // see the comment in fgetc
    if (stream->current_mode == O_RDONLY)
        fflush(stream);

    stream->current_mode = O_WRONLY;

    if (stream->buffered == _IONBF) {
        ssize_t written = write(stream->fd, s, strlen(s));
        if (written < 0) {
            stream->was_error = 1;
            ret = EOF;
        } else if (written != strlen(s))
            stream->was_eof = 1;
        if (written > 0)
            stream->current_offset += written;
    } else {
        for (size_t i = 0; i < strlen(s); i++) {
            if (file_put_buffer(stream, s[i]) == EOF) {
                ret = EOF;
                break;
            }
        }
    }

    pthread_mutex_unlock(&stream->mutex);
    return ret;
}

int ungetc(int c, FILE *stream) {
    if (c == EOF) return EOF;
    if (stream == NULL) {
        ___set_errno(EBADF);
        return EOF;
    }
    assert(!pthread_mutex_lock(&stream->mutex));
    if (FULL(&stream->ungetc_buf, FILE_UNGETC_ARR_SIZE)) {
        pthread_mutex_unlock(&stream->mutex);
        return EOF;
    }
    stream->ungetc_buf.buf[stream->ungetc_buf.tail] = c;
    INC(&stream->ungetc_buf, FILE_UNGETC_ARR_SIZE);
    stream->current_offset --;
    stream->was_eof = 0;
    pthread_mutex_unlock(&stream->mutex);
    return c;
}
int getchar() {
    return fgetc(stdin);
}
int putchar(int c) {
    return fputc(c, stdout);
}

// we use recursive mutexes, so stuff already works with flockfile

int getc_unlocked(FILE *stream) {
    return fgetc(stream);
}
int getchar_unlocked() {
    return getchar();
}
int putc_unlocked(int c, FILE *stream) {
    return fputc(c, stream);
}
int putchar_unlocked(int c) {
    return putchar(c);
}