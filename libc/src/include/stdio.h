#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include "sys/types.h"

#define PRINTF_MAX_FORMAT_OUT 128

#define EOF (-1)

#define FILE_UNGETC_ARR_SIZE 128

#define BUFSIZ 1024
#define FOPEN_MAX 0xFFFFFFFF // no limit
#define FILENAME_MAX 4096 // from limits_local.h

struct FILE {
    int fd;
    pthread_mutex_t mutex;
    char buffered; // _IONBF for fmemopened fi
    char pure_buf; // for fmemopen
    // input/output, use O_RDONLY and O_WRONLY to get,
    // 0 means neither (after fflush or fseek, etc.)
    char current_mode;
    struct {
        char alloced_buf; // if we can/can't do realloc/free on buf
        unsigned char * buf;
        size_t head;
        size_t tail;
        size_t size;
    } buf;
    struct {
        unsigned char buf[FILE_UNGETC_ARR_SIZE];
        size_t head;
        size_t tail;
    } ungetc_buf;
    unsigned short mode; // so that we can decline fwrite without going to the kernel

    char seekable;
    off_t current_offset; // the "virtual" offset we keep by using our buffers

    // fread/fwrite/whatever return -1 on both EOF and EIO
    // call feof()/ferror() to figure out which
    unsigned char was_eof : 1;
    unsigned char was_error : 1;


    struct FILE * next, * prev; // for fclosing everything on exit
} typedef FILE;

typedef off_t fpos_t;

FILE * fopen(const char *__restrict pathname, const char *__restrict mode);
FILE * fdopen(int fildes, const char *mode);
FILE * fmemopen(void *restrict buf, size_t max_size, const char *restrict mode);

int fclose(FILE *stream);

void setbuf(FILE *__restrict stream, char *__restrict buf);
int setvbuf(FILE *__restrict stream, char *__restrict buf, int type, size_t size);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);

int fileno(FILE *stream);

long ftell(FILE *stream);
off_t ftello(FILE *stream);

void flockfile(FILE *file);
int ftrylockfile(FILE *file);
void funlockfile(FILE *file);

int fseek(FILE *stream, long offset, int whence);
int fseeko(FILE *stream, off_t offset, int whence);
void rewind(FILE *stream);

int fgetpos(FILE *__restrict stream, fpos_t *__restrict pos);
int fsetpos(FILE *stream, const fpos_t *pos);

int fflush(FILE *stream);

int fgetc(FILE *stream);
int getc(FILE *stream); // just to provide the prototype
#define getc(stream) fgetc(stream)

char *fgets(char *__restrict s, int n, FILE *__restrict stream);

int fputc(int c, FILE *stream);
int putc(int c, FILE *stream); // just to provide the prototype
#define putc(c, stream) fputc(c, stream)

int fputs(const char *__restrict s, FILE *__restrict stream);

size_t fread(void *__restrict ptr, size_t size, size_t nitems, FILE *__restrict stream);
size_t fwrite(const void *__restrict ptr, size_t size, size_t nitems, FILE *__restrict stream);

int ungetc(int c, FILE *stream);

int getchar();
int putchar(int c);

int getc_unlocked(FILE *stream);
int getchar_unlocked();
int putc_unlocked(int c, FILE *stream);
int putchar_unlocked(int c);

#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE * stdin, * stdout, * stderr;

int vfprintf(FILE * __restrict stream, const char * __restrict format, va_list args);
int __attribute__((format(printf, 2, 3))) fprintf(FILE * __restrict stream, const char * __restrict format, ...);

int vsprintf(char * __restrict s, const char * __restrict format, va_list args);
int __attribute__((format(printf, 2, 3))) sprintf(char * __restrict s, const char * __restrict format, ...);

int vsnprintf(char * __restrict s, size_t size, const char * __restrict format, va_list args);
int __attribute__((format(printf, 3, 4))) snprintf(char * __restrict s, size_t size, const char * __restrict format, ...);

int __attribute__((format(printf, 1, 2))) printf(const char * format, ...);

int __attribute__((format(scanf, 1, 2))) scanf(const char * format, ...);
int __attribute__((format(scanf, 2, 3))) fscanf(FILE * __restrict stream, const char * __restrict format, ...);

int vscanf(const char * format, va_list args);
int vfscanf(FILE * __restrict stream, const char * __restrict format, va_list args);

int __attribute__((format(scanf, 2, 3))) sscanf(const char * s, const char * format, ...);
int vsscanf(const char * s, const char * format, va_list args);

void perror(const char * s);
#endif