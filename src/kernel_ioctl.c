#include "include/kernel.h"
#include "include/fs/fs.h"
#include "include/kernel_sched.h"
#include "include/kernel_ioctl.h"
#include "include/errno.h"

extern long tty_ioctl(dev_t dev, unsigned long cmd, unsigned long arg);

static ioctl_func_t ioctl_func_table[DEV_MAJ_LIMIT] = {
    //[DEV_MEM] = 
    //[DEV_BLOCK] = 
    [DEV_MAJ_TTY] = tty_ioctl,
    [DEV_MAJ_TTY_META] = tty_ioctl,
};

long sys_ioctl(unsigned int fd, unsigned long command, unsigned long arg) {
    if (fd >= FD_LIMIT_PROCESS) return EBADF;
    file_descriptor_t * file = current_process->fds[fd];
    if (file == NULL) return EBADF;

}