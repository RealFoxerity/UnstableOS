#ifndef _TERMIOS_H
#define _TERMIOS_H

#define NCCS 11
typedef unsigned char cc_t;
typedef unsigned short tcflag_t;
//typedef unsigned int speed_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;

    cc_t c_cc[NCCS];
};

// NC non-canonical, IC canonical ("line buffered")
#define VEOF   0  // IC       if ICANON all bytes immediately sent to process (as if \n was entered)
#define VEOL   1  // IC       if ICANON another \n
#define VERASE 2  // IC       if not ICANON works as backspace (until EOF, EOL, \n)
#define VINTR  3  // IC, NC   sigint
#define VKILL  4  // IC       if ICANON deletes entire line (until EOF, EOL, \n)
#define VMIN   5  // NC       minimum bytes to satisfy read for non-canonical mode
#define VQUIT  6  // IC, NC   sigquit
#define VSTART 7  // IC, NC   if flow control starts output again
#define VSTOP  8  // IC, NC   if flow control stops output
#define VSUSP  9  // IC, NC   sigtstp to foreground pgrp
#define VTIME  10 // NC       timeout value for non-canonical mode


// as you might have noticed, i pick from the posix standard based on how easy things are to implement
#define IBRKINT 1 // break condition (see rs232 break status, unsupported) sends SIGINT
#define ICRNL   2 // carriage return -> new line
#define IGNBRK  4 // ignoring break condition, translating into NULL byte
#define IGNCR   8 // ignore carriage return
//#define IGNPAR  16 // ignore characters with parity error
#define INLCR   32 // new line -> carriage return
//#define INPCK   64 // enable parity check
#define ISTRIP  128 // strip 8 bit ascii to 7 bit
#define IXANY   256 // any received character resumes output
//#define IXOFF   512 //  VSTOP/VSTART to pause/resume character input when internal buffer full? TODO: implement
#define IXON    1024 // VSTOP/VSTART to pause/resume character output


#define OPOST  1 // whether to even do processing
#define ONLCR  2 // new line -> carriage return new line
#define OCRNL  4 // carriage return -> new line
#define ONOCR  8
#define ONLRET 16 // new line resets tty column counter to 0
//nldly, crdly, tabdly, bsdly, vtdly


/*
#define  CSIZE 0x3  // character size mask
#define    CS5 0x0  // 5 bits per character
#define    CS6 0x1  // 6 bits per character
#define    CS7 0x2  // 7 bits per character
#define    CS8 0x3  // 8 bits per character
#define CSTOPB 0x4  // 2 stop bits
#define CREAD  0x8  // enable reciever
#define PARENB 0x10 // parity enable
#define PARODD 0x20 // odd parity, else even
#define HUPCL  0x40 // send hangup on last FD close
#define CLOCAL 0x80 // ignore modem lines
*/


#define ECHO   1
#define ECHOE  2  // do VERASE visually (as '\b \b') - the standard backspace behavior
#define ECHOK  4  // do VKILL visually - the standard CTRL+U behavior
#define ECHONL 8  // echo \n even if ECHO is disabled
#define ICANON 16 // newline buffered, control chars handling
#define ISIG   32 // enable signals
#define NOFLSH 64 // don't do flushing on VINTR, VQUIT, VSUSP
#define TOSTOP 128 // set SIGTTOU if background process group tries to write()
#define ECHOCTL 256 // echo escapes as ^X

/*
#define B0     0
#define B50    50
#define B75    75
#define B110   110
#define B134   134
#define B150   150
#define B200   200
#define B300   300
#define B600   600
#define B1200  1200
#define B1800  1800
#define B2400  2400
#define B4800  4800
#define B9600  9600
#define B19200 19200
#define B38400 38400
*/

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

int tcgetattr(int fildes, struct termios *termios_p);
int tcsetattr(int fildes, int optional_actions, const struct termios *termios_p);

#include <sys/types.h>
pid_t tcgetsid(int fildes);

#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3
int tcflow(int fildes, int action);

#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2
int tcflush(int fildes, int queue_selector);

// we do blocking output to ttys anyway, so this is just a stub returning ENOSYS
int tcdrain(int fildes);
// currently no support for sending breaks, so also just a ENOSYS
int tcsendbreak(int fildes);
#endif