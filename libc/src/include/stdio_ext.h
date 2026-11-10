#ifndef _STDIO_EXT_H
#define _STDIO_EXT_H
// Solaris + GNU functions

#include <stdio.h>
size_t __fbufsize(FILE *stream);
size_t __fpending(FILE *stream);
int __flbf(FILE *stream);
int __freadable(FILE *stream);
int __fwritable(FILE *stream);
int __freading(FILE *stream);
int __fwriting(FILE *stream);
//int __fsetlocking(FILE *stream, int type);
void _flushlbf();
void __fpurge(FILE *stream);
void __fseterr(FILE *stream);
#endif