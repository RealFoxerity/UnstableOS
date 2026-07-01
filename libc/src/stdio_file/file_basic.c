#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include <string.h>
#include <assert.h>

#include <limits.h>

FILE * stdin = NULL;
FILE * stdout = NULL;
FILE * stderr = NULL;

#define UNLINK_DOUBLE_LINKED_LIST(item, list) do {  \
    if ((item)->next != NULL)                       \
        (item)->next->prev = (item)->prev;          \
    else                                            \
        (list)->prev = (item)->prev;                \
    if ((item) != (list))                           \
        (item)->prev->next = (item)->next;          \
    else                                            \
        (list) = (item)->next;                      \
} while (0);

#define APPEND_DOUBLE_LINKED_LIST(item, list) do {  \
    (item)->next = NULL;                            \
    if ((list) == NULL) {                           \
        (list) = (item);                            \
    } else {                                        \
        (item)->prev = (list)->prev;                \
        (list)->prev->next = (item);                \
    }                                               \
    (list)->prev = (item);                          \
} while (0);

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


FILE * __files = NULL;
pthread_mutex_t __files_lock;

#define OCREAT_FLAGS_FOPEN (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

static int fopen_get_flags(const char * mode) {
    unsigned short flags = 0;
    switch (mode[0]) {
        case 'r':
            flags |= O_RDONLY;
            break;
        case 'w':
            flags |= O_WRONLY | O_CREAT | O_TRUNC;
            break;
        case 'a':
            flags |= O_WRONLY | O_CREAT | O_APPEND;
            break;
        default:
            ___set_errno(EINVAL);
            return -1;
    }
    char c;
    while ((c = *++mode) != '\0') {
        switch (c) {
            case 'b':
                // nothing, we're not windows :P
                break;
            case 'e':
                flags |= O_CLOEXEC;
                break;
            case 'x':
                flags |= O_EXCL;
                break;
            case '+':
                flags |= O_RDWR;
                break;
            default:
                ___set_errno(EINVAL);
                return -1;
        }
    }
    return flags;
}

static FILE * fdopen_parsed(int fildes, int flags) {
    FILE * new_file = malloc(sizeof(FILE));
    if (new_file == NULL) {
        ___set_errno(ENOMEM);
        return NULL;
    }

    memset(new_file, 0, sizeof(FILE));
    new_file->fd = fildes;
    new_file->mode = flags;

    if (isatty(fildes))
        new_file->buffered = _IOLBF;

    new_file->buf.buf = malloc(BUFSIZ);
    if (new_file->buf.buf == NULL) {
        free(new_file);
        ___set_errno(ENOMEM);
        return NULL;
    }
    new_file->buf.size = BUFSIZ;
    new_file->buf.alloced_buf = 1;

    // a noop, but for non-seekable will return -1 with ESPIPE in errno
    if (lseek(new_file->fd, 0, SEEK_CUR) == -1)
        new_file->seekable = 0;
    else
        new_file->seekable = 1;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&new_file->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    if (flags & O_CLOEXEC)
        fcntl(fildes, F_SETFD, FD_CLOEXEC);
    if (flags & O_APPEND)
        fcntl(fildes, F_SETFL, O_APPEND);

    pthread_mutex_lock(&__files_lock);
    APPEND_DOUBLE_LINKED_LIST(new_file, __files);
    pthread_mutex_unlock(&__files_lock);
    return new_file;
}

FILE * fmemopen(void *restrict buf, size_t max_size, const char *restrict mode) {
    if (mode == NULL || max_size == 0) {
        ___set_errno(EINVAL);
        return NULL;
    }
    char alloced = 0;
    int flags = fopen_get_flags(mode);
    off_t off = 0;
    if (flags == -1) {
        ___set_errno(EINVAL);
        return NULL;
    }
    if (buf == NULL) {
        buf = malloc(max_size);
        alloced = 1;
    } else if (mode[0] == 'w') {
        *(char*)buf = '\0';
    } else if (mode[0] == 'a') {
        for (char * i = buf; i < (char*)buf + max_size; i++) {
            if (*i == '\0') {
                off = (void*)i - buf;
                break;
            }
        }
    }
    if (buf == NULL) {
        ___set_errno(ENOMEM);
        return NULL;
    }

    FILE * new_file = malloc(sizeof(FILE));
    if (new_file == NULL) {
        if (alloced) free(buf);
        ___set_errno(ENOMEM);
        return NULL;
    }
    memset(new_file, 0, sizeof(FILE));
    new_file->pure_buf = 1;
    new_file->mode = flags;

    new_file->buffered = _IONBF;

    new_file->buf.buf = buf;
    new_file->buf.size = max_size;
    new_file->buf.alloced_buf = alloced;

    new_file->seekable = 1;
    new_file->current_offset = off;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&new_file->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    pthread_mutex_lock(&__files_lock);
    APPEND_DOUBLE_LINKED_LIST(new_file, __files);
    pthread_mutex_unlock(&__files_lock);
    return new_file;
}

FILE * fdopen(int fildes, const char *mode) {
    if (mode == NULL || fildes < 0) {
        ___set_errno(EINVAL);
        return NULL;
    }

    int flags = fopen_get_flags(mode);
    if (flags == -1)
        return NULL;

    return fdopen_parsed(fildes, flags);
}
FILE * fopen(const char *restrict pathname, const char *restrict mode) {
    if (pathname == NULL || mode == NULL) {
        ___set_errno(EINVAL);
        return NULL;
    }
    int flags = fopen_get_flags(mode);
    if (flags == -1)
        return NULL;

    int fd = open(pathname, flags, OCREAT_FLAGS_FOPEN);
    if (fd == -1)
        return NULL;

    FILE * out = fdopen_parsed(fd, flags);
    if (out == NULL)
        close(fd);
    return out;
}

// supposed to be called from __libc_init
void __stdio_init() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&__files_lock, &attr);
    pthread_mutexattr_destroy(&attr);

    stdin = fdopen(0, "r");
    stdout = fdopen(1, "w");
    stderr = fdopen(2, "w");
    assert(stdin && stdout && stderr);
}

// supposed to be called from exit()
void __stdio_deinit() {
    assert(!pthread_mutex_lock(&__files_lock));
    while (__files) fclose(__files);
    // leaking lock to prevent creating new streams
}

int fclose(FILE *stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return EOF;
    }

    // so that exit() from a different thread doesn't run into UAF on this file
    assert(!pthread_mutex_lock(&__files_lock));
    assert(!pthread_mutex_lock(&stream->mutex));

    int success = fflush(stream);
    if (stream->buf.alloced_buf)
        free(stream->buf.buf);
    if (!stream->pure_buf)
        close(stream->fd);
    pthread_mutex_destroy(&stream->mutex);
    UNLINK_DOUBLE_LINKED_LIST(stream, __files);
    free(stream);
    pthread_mutex_unlock(&__files_lock);

    return success;
}
void clearerr(FILE *stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return;
    }
    stream->was_eof = stream->was_error = 0;
}
int feof(FILE *stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    return stream->was_eof;
}
int ferror(FILE *stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    return stream->was_error;
}

int fileno(FILE *stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    return stream->fd;
}

void flockfile(FILE *file) {
    if (file == NULL) {
        ___set_errno(EBADF);
        return;
    }
    assert(!pthread_mutex_lock(&file->mutex));
}
int ftrylockfile(FILE *file) {
    if (file == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    return pthread_mutex_trylock(&file->mutex);
}
void funlockfile(FILE *file) {
    if (file == NULL) {
        ___set_errno(EBADF);
        return;
    }
    pthread_mutex_unlock(&file->mutex);
}

static unsigned char file_get_ungetc(FILE * stream) {
    DEC_LAST(&stream->ungetc_buf, FILE_UNGETC_ARR_SIZE);
    stream->current_offset ++;
    return stream->ungetc_buf.buf[stream->ungetc_buf.tail];
}

int fflush(FILE *stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return EOF;
    }

    assert(!pthread_mutex_lock(&stream->mutex));
    stream->ungetc_buf.head = stream->ungetc_buf.tail = 0;

    if (stream->current_offset < 0)
        stream->current_offset = 0; // from ungetc

    if (stream->pure_buf) {
        stream->current_mode = 0;
        pthread_mutex_unlock(&stream->mutex);
        return 0;
    }

    if (stream->current_mode == O_RDONLY) {
        stream->current_mode = 0;
        stream->buf.head = stream->buf.tail = 0;
        if (stream->seekable) {
            off_t ret = lseek(stream->fd, stream->current_offset, SEEK_SET);
            if (ret == -1) {
                stream->was_error = 1;
                pthread_mutex_unlock(&stream->mutex);
                return EOF;
            }
            if (ret < stream->current_offset) {
                stream->current_offset = ret;
                stream->was_eof = 1;
                pthread_mutex_unlock(&stream->mutex);
                return EOF;
            }
        }
    } else if (stream->current_mode == O_WRONLY) {
        stream->current_mode = 0;
        if (EMPTY(&stream->buf)) {
            pthread_mutex_unlock(&stream->mutex);
            return 0;
        }
        size_t written = 0;
        if (stream->buf.tail > stream->buf.head) {
            written = write(stream->fd,
                stream->buf.buf + stream->buf.head,
                stream->buf.tail - stream->buf.head);
        } else {
            written = write(stream->fd,
                stream->buf.buf + stream->buf.head,
                stream->buf.size - stream->buf.head);
            written += write(stream->fd,
                stream->buf.buf,
                stream->buf.tail);
        }
        size_t original_size = REMAIN(&stream->buf, stream->buf.size);
        stream->buf.head = stream->buf.tail = 0;
        if (written < original_size) {
            // error or incomplete write on a stream is ub
            stream->was_error = 1;
            pthread_mutex_unlock(&stream->mutex);
            return EOF;
        }
    } else {
        // some fucked up state, just a fallback piece of code
        stream->current_mode = 0;
        stream->buf.head = stream->buf.tail = 0;
    }
    pthread_mutex_unlock(&stream->mutex);
    return 0;
}

size_t fread(void *restrict ptr, size_t size, size_t nitems, FILE *restrict stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return 0;
    }
    if (ptr == NULL) {
        ___set_errno(EINVAL);
        return 0;
    }

    if (!(stream->mode & O_RDONLY)) {
        ___set_errno(EBADF);
        return 0;
    }

    size_t total_len = nitems * size;
    if (total_len == 0)
        return 0;

    assert(!pthread_mutex_lock(&stream->mutex));

    // prepare for read
    // buffer was used to write
    if (stream->current_mode == O_WRONLY) {
        // I assume we didn't just magically get into write mode if writes are disabled
        // it's UB to fread() immediately following a fwrite() without positioning function or fflush()
        fflush(stream);
    }
    stream->current_mode = O_RDONLY;

    // first clear LIFO ungetc queue
    // we do the same in fgetc, but we need it here anyway for the unbuffered read
    while (total_len > 0 && !EMPTY(&stream->ungetc_buf)) {
        *(unsigned char*)ptr++ = file_get_ungetc(stream);
        total_len--;
    }
    if (total_len == 0)
        goto unlock;

    if (stream->buffered == _IONBF) {
        if (stream->pure_buf) {
            if (stream->current_offset == stream->buf.size) {
                stream->was_eof = 1;
                goto unlock;
            }

            if (total_len > stream->buf.size - stream->current_offset) {
                total_len -= stream->buf.size - stream->current_offset;
                memcpy(ptr,
                    stream->buf.buf + stream->current_offset,
                    stream->buf.size - stream->current_offset);
                stream->current_offset = stream->buf.size;
                stream->was_eof = 1;
            } else {
                memcpy(ptr,
                    stream->buf.buf + stream->current_offset,
                    total_len);
                stream->current_offset += total_len;
                total_len = 0;
            }
            goto unlock;
        }

        ssize_t read_bytes = read(stream->fd, ptr, total_len);
        if (read_bytes == 0)
            stream->was_eof = 1;
        else if (read_bytes < 0)
            stream->was_error = 1;
        else {
            stream->current_offset += read_bytes;
            if (read_bytes != total_len)
                stream->was_eof = 1;
            total_len -= read_bytes;
        }
        goto unlock;
    }

    while (total_len > 0) {
        int c = fgetc(stream);
        if (c == EOF)
            break;
        *(unsigned char*)ptr++ = (unsigned char)c;
        total_len--;
    }

    unlock:
    pthread_mutex_unlock(&stream->mutex);

    return (nitems * size - total_len)/size;
}

// redefined here to avoid the calling overhead of putc
// fgetc didn't get this treatment because that has a more complicated approach
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

size_t fwrite(const void *restrict ptr, size_t size, size_t nitems, FILE *restrict stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return 0;
    }
    if (ptr == NULL) {
        ___set_errno(EINVAL);
        return 0;
    }

    if (!(stream->mode & O_WRONLY)) {
        ___set_errno(EINVAL);
        return 0;
    }

    size_t total_len = nitems * size;
    if (total_len == 0)
        return 0;

    assert(!pthread_mutex_lock(&stream->mutex));

    // see the comment in fread
    if (stream->current_mode == O_RDONLY) {
        fflush(stream);
    }
    stream->current_mode = O_WRONLY;

    if (stream->buffered == _IONBF) {
        if (stream->pure_buf) {
            if (stream->current_offset == stream->buf.size) {
                stream->was_eof = 1;
                goto unlock;
            }

            if (total_len > stream->buf.size - stream->current_offset) {
                total_len -= stream->buf.size - stream->current_offset;
                memcpy(stream->buf.buf + stream->current_offset,
                    ptr,
                    stream->buf.size - stream->current_offset);
                stream->current_offset = stream->buf.size;
                stream->was_eof = 1;
            } else {
                memcpy(stream->buf.buf + stream->current_offset,
                    ptr,
                    total_len);
                stream->current_offset += total_len;
                total_len = 0;
            }
            goto unlock;
        }
        ssize_t written_bytes = write(stream->fd, ptr, total_len);
        if (written_bytes < 0)
            stream->was_error = 1;
        else {
            stream->current_offset += written_bytes;
            if (written_bytes != total_len)
                stream->was_eof = 1;
            total_len -= written_bytes;
        }
        goto unlock;
    }

    while (total_len > 0) {
        if (file_put_buffer(stream, *(unsigned char*)ptr++) == EOF)
            break;
        total_len--;
    }

    unlock:
    pthread_mutex_unlock(&stream->mutex);
    return (nitems * size - total_len)/size;
}

void setbuf(FILE *restrict stream, char *restrict buf) {
    setvbuf(stream, buf, buf == NULL ? _IONBF : _IOFBF, BUFSIZ);
}

#define SETVBUF_MINIMUM 16
int setvbuf(FILE *restrict stream, char *restrict buf, int type, size_t size) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    if (size < SETVBUF_MINIMUM || stream->pure_buf) {
        ___set_errno(EINVAL);
        return -1;
    }
    switch (type) {
        case _IONBF:
        case _IOFBF:
        case _IOLBF:
            break;
        default:
            ___set_errno(EINVAL);
            return -1;
    }
    // it's UB to call setvbuf() on anything that's been used since fopen()
    // however we try to make it work anyway

    assert(!pthread_mutex_lock(&stream->mutex));
    if (!EMPTY(&stream->buf))
        fflush(stream);
    if (stream->buf.alloced_buf)
        free(stream->buf.buf);
    stream->buf.size = size;
    if (buf)
        stream->buf.buf = (unsigned char*)buf;
    else
        stream->buf.buf = malloc(size);

    if (stream->buf.buf == NULL) {
        ___set_errno(ENOMEM);
        stream->buffered = _IONBF;
        pthread_mutex_unlock(&stream->mutex);
        return -1;
    }
    stream->buffered = (char)type;
    pthread_mutex_unlock(&stream->mutex);
    return 0;
}