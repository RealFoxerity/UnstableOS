#ifndef DEVS_H
#define DEVS_H


typedef unsigned short dev_t; // major << 10 | minor   major is then 0-64 and minor 1024
#define MAJOR(dev) (dev >> 10)
#define MINOR(dev) (dev & 0x3FF)
#define GET_DEV(major, minor) ((major << 10) | (minor & 0x3FF))

#define DEV_MAJ_LIMIT (1<<6) // maximum of device major numbers allowed according to our dev_t type

enum dev_maj {
    DEV_MEM,
    DEV_BLOCK,
    DEV_TTY,
};
enum dev_tty_min {
    DEV_TTY_CONSOLE, // tty1 + ttyS0
    
    DEV_TTY_S0 = DEV_TTY_CONSOLE, // Serial backed terminals
    DEV_TTY_S1,
    DEV_TTY_S3,
    DEV_TTY_S4,

    DEV_TTY_1 = DEV_TTY_CONSOLE, // VGA backed terminals
    DEV_TTY_2 = 8,
    DEV_TTY_3,
    DEV_TTY_4,
};


#endif