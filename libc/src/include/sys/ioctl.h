#ifndef _IOCTL_H
#define _IOCTL_H

int ioctl(int fildes, unsigned long request, ...); // unistd.c




#include <UnstableOS/devs.h>

#define __IOCTL_NO(major, func) ((((major) & 0x3F) << 10) | ((func) & 0x3FF))
#define __IOCTL_DEV(id) ((id) >> 10)

/* TTY ioctl request identifiers */
#define TCGETS    __IOCTL_NO(DEV_MAJ_TTY, 0) // gets the termios structure -> tcgetattr
#define TCSETS    __IOCTL_NO(DEV_MAJ_TTY, 1) // sets termios immediately   -> tcsetattr with TCSANOW
#define TCSETSW   __IOCTL_NO(DEV_MAJ_TTY, 2) // after all written          -> tcsetattr with TCSADRAIN
#define TCSETSF   __IOCTL_NO(DEV_MAJ_TTY, 3) // after all written and input discarded -> tcsetattr with TCSAFLUSH

#define TCXONC    __IOCTL_NO(DEV_MAJ_TTY, 4) // tcflow
#define TCFLSH    __IOCTL_NO(DEV_MAJ_TTY, 5) // tcflush

#define TIOCGPGRP __IOCTL_NO(DEV_MAJ_TTY, 6) // get fg proc group -> tcgetpgrp
#define TIOCSPGRP __IOCTL_NO(DEV_MAJ_TTY, 7) // set               -> tcsetpgrp
#define TIOCGSID  __IOCTL_NO(DEV_MAJ_TTY, 8) // get session id    -> tcgetsid


#endif