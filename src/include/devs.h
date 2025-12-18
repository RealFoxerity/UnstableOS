#ifndef DEVS_H
#define DEVS_H


typedef unsigned short dev_t; // major << 10 | minor   major is then 0-64 and minor 1024
#define MAJOR(dev) (dev >> 10)
#define MINOR(dev) (dev & 0x3FF)
#define GET_DEV(major, minor) (((major) << 10) | ((minor) & 0x3FF))

#define DEV_MAJ_LIMIT (1<<6) // maximum of device major numbers allowed according to our dev_t type

enum dev_maj {
    DEV_MAJ_MEM,
    DEV_MAJ_BLOCK,
    DEV_MAJ_TTY,
    DEV_MAJ_TTY_META, // not exact devices but instead mnemonics
};


#define __TTY_CONSOLE 16
#define __TTY_SERIAL 8
#define __TTY_PTY 20
#define TTY_LIMIT_KERNEL (__TTY_SERIAL + __TTY_CONSOLE + __TTY_PTY) // maximum amount of ttys opened

enum dev_tty_min {
    DEV_TTY_0, // VGA backed terminals
    DEV_TTY_1,
    DEV_TTY_2,
    DEV_TTY_3,
 
    /*
    DEV_TTY_4 - DEV_TTY_15
    */

    DEV_TTY_S0 = __TTY_CONSOLE, // Serial backed terminals
    /*
    DEV_TTY_S1 - DEV_TTY_7
    */

    DEV_PTY_0 = __TTY_CONSOLE + __TTY_SERIAL,
    /*
    DEV_PTY_1 - DEV_PTY_19
    */
};

enum dev_tty_meta_min {
    DEV_TTY_CONSOLE, // /dev/console, kernel console
    DEV_TTY_CURRENT, // /dev/tty, currently used tty
};


#endif