#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <string.h>
#include <assert.h>

#include <limits.h>

long ftell(FILE *stream) {
    off_t offset = ftello(stream);
    if (offset > LONG_MAX) {
        ___set_errno(EOVERFLOW);
        offset = -1;
    }
    return (long)offset;
}
off_t ftello(FILE *stream) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    if (!stream->seekable) {
        ___set_errno(ESPIPE);
        return -1;
    }

    assert(!pthread_mutex_lock(&stream->mutex));

    off_t offset = stream->current_offset;

    pthread_mutex_unlock(&stream->mutex);

    return offset;
}

int fseek(FILE *stream, long offset, int whence) {
    return fseeko(stream, offset, whence);
}
int fseeko(FILE *stream, off_t offset, int whence) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    if (!stream->seekable) {
        ___set_errno(ESPIPE);
        return -1;
    }
    switch (whence) {
        case SEEK_SET:
        case SEEK_CUR:
        case SEEK_END:
            break;
        default:
            ___set_errno(EINVAL);
            return -1;
    }

    assert(!pthread_mutex_lock(&stream->mutex));

    if (stream->pure_buf) {
        fflush(stream);
        switch (whence) {
            case SEEK_CUR:
                offset += stream->current_offset;
            case SEEK_SET:
                if (offset > stream->buf.size || offset < 0) {
                    ___set_errno(EINVAL);
                    pthread_mutex_unlock(&stream->mutex);
                    return -1;
                }
                stream->current_offset = offset;
                break;
            case SEEK_END:
                if (offset > 0 || -offset > stream->buf.size) {
                    ___set_errno(EINVAL);
                    pthread_mutex_unlock(&stream->mutex);
                    return -1;
                }
                stream->current_offset = stream->buf.size - offset;
            default: break;
        }
        goto unlock;
    }

    off_t new_off;
    if (fflush(stream) != 0 ||
        (new_off = lseek(stream->fd, offset, whence)) < 0
    ) {
        pthread_mutex_unlock(&stream->mutex);
        return -1;
    }
    stream->current_offset = new_off;

    unlock:
    pthread_mutex_unlock(&stream->mutex);
    return 0;
}
void rewind(FILE *stream) {
    fseeko(stream, 0, SEEK_SET);
}

int fgetpos(FILE *__restrict stream, fpos_t *__restrict pos) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    if (pos == NULL) {
        ___set_errno(EINVAL);
        return -1;
    }
    if (stream->seekable) {
        ___set_errno(ESPIPE);
        return -1;
    }
    assert(!pthread_mutex_lock(&stream->mutex));
    *pos = stream->current_offset;
    pthread_mutex_unlock(&stream->mutex);
    return 0;
}
int fsetpos(FILE *stream, const fpos_t *pos) {
    if (stream == NULL) {
        ___set_errno(EBADF);
        return -1;
    }
    if (pos == NULL) {
        ___set_errno(EINVAL);
        return -1;
    }
    return fseeko(stream, *pos, SEEK_SET);
}