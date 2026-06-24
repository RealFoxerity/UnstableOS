#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

#include <errno.h>

int tcgetattr(int fildes, struct termios *termios_p) {
    return ioctl(fildes, TCGETS, termios_p);
}

int tcsetattr(int fildes, int optional_actions, const struct termios *termios_p) {
    switch(optional_actions) {
        case TCSANOW:
            return ioctl(fildes, TCSETS , termios_p);
        case TCSADRAIN:
            return ioctl(fildes, TCSETSW, termios_p);
        case TCSAFLUSH:
            return ioctl(fildes, TCSETSF, termios_p);
        default:
            ___set_errno(EINVAL);
            return -1;
    }
}

int isatty(int fildes) {
    struct termios tty_info;
    if (ioctl(fildes, TCGETS, &tty_info) < 0)
        return 0;
    return 1;
}

pid_t tcgetpgrp(int fildes) {
    return ioctl(fildes, TIOCGPGRP);
}

int tcsetpgrp(int fildes, pid_t pgid_id) {
    return ioctl(fildes, TIOCSPGRP, pgid_id);
}

pid_t tcgetsid(int fildes) {
    return ioctl(fildes, TIOCGSID);
}

int tcflow(int fildes, int action) {
    switch (action) {
        case TCOOFF:
        case TCOON:
        case TCIOFF:
        case TCION:
            return ioctl(fildes, TCXONC, action);
        default:
            ___set_errno(EINVAL);
            return -1;
    }
}

int tcflush(int fildes, int queue_selector) {
    switch (queue_selector) {
        case TCIFLUSH:
        case TCOFLUSH:
        case TCIOFLUSH:
            return ioctl(fildes, TCFLSH, queue_selector);
        default:
            ___set_errno(EINVAL);
            return -1;
    }
}

int tcdrain(int fildes) {
    if (fildes < 0) {
        ___set_errno(EBADF);
    } else {
        ___set_errno(ENOSYS);
    }
    return -1;
}

int tcsendbreak(int fildes) {
    if (fildes < 0) {
        ___set_errno(EBADF);
    } else {
        ___set_errno(ENOSYS);
    }
    return -1;
}