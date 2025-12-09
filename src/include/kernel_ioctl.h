#ifndef KERNEL_IOCTL_H
#define KERNEL_IOCTL_H

#include "fs/fs.h"
#include "kernel.h"

long sys_ioctl(unsigned int fd, unsigned long command, unsigned long arg);

typedef long (*ioctl_func_t)(dev_t dev, unsigned long command, unsigned long arg);

#endif