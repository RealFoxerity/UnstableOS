#ifndef _IOCTL_H
#define _IOCTL_H

int ioctl(int fildes, unsigned long request, ...); // unistd.c


#define __IOCTL_NO(major, func) ((((major) & 0x3F) << 10) | ((func) & 0x3FF))
#define __IOCTL_DEV(id) ((id) >> 10)

#include <bits/ioctl/tty_ioctl.h>
#include <bits/ioctl/fb_ioctl.h>

#endif