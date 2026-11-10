#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#include <sys/select.h>

int gettimeofday(struct timeval *__restrict tp, void *__restrict tzp);
#endif