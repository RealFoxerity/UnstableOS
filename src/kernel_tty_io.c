#include "include/kernel_tty_io.h"
#include "include/devs.h"
#include "include/errno.h"
#include "include/fs/fs.h"
#include "include/kernel.h"
#include "include/vga.h"

extern size_t tty_com_write(tty_t * tty); // from rs232.c

static tty_t kernel_console = {
    .used = 1,
    .com_port = 0,
    .height = VGA_HEIGHT,
    .width = VGA_WIDTH,
    .session = 0,
    .write = tty_com_write
};

tty_t * terminals[TTY_LIMIT_KERNEL] = {
    [DEV_TTY_CONSOLE] = &kernel_console,
    //[DEV_TTY_S0] = &kernel_console,
    //[DEV_TTY_1] = &kernel_console,


};

// terminals[0] - terminals[3] = vga framebuffer backed tty devices, lctrl+rctrl+1-4

//static inline tty_t * tty_alloc(dev_t dev) {
//
//}

inode_t * tty_alloc_console() { // for the kernel task, don't call for user processes
    inode_t * tty_inode = get_free_inode();
    kassert(tty_inode);


}

long tty_ioctl(dev_t dev, unsigned long cmd, unsigned long arg);

long tty_read();

long tty_write(dev_t dev, const char * s, size_t n) {
    if (MAJOR(dev) != DEV_TTY) return EINVAL;
    if (MINOR(dev) >= TTY_LIMIT_KERNEL) return EINVAL;
    if (terminals[MINOR(dev)] == NULL) return EIO;
    if (terminals[MINOR(dev)]->write == NULL) return EIO;
    
    if (MINOR(dev) == DEV_TTY_CONSOLE) { // have to write to multiple different devices (serial, tty 1), assumes the kernel console has the serial write() function
        vga_write(s, n); // this unfortunately writes the kernel log to any currently active tty, todo: rewrite vga_write        
    }

    return terminals[MINOR(dev)]->write(terminals[MINOR(dev)]);
}
//long tty_read()