#ifndef _TIMES_H
#define _TIMES_H

#include "types.h"

struct tms {
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};

clock_t times(struct tms * buffer);

#endif