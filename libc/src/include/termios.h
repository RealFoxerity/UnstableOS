#ifndef _TERMIOS_H
#define _TERMIOS_H

#define NCCS 11
typedef unsigned char cc_t;
typedef unsigned short tcflag_t;
typedef unsigned short speed_t;

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
#define BRKINT 0x1    // break condition (see rs232 break status) sends SIGINT
#define ICRNL  0x2    // carriage return -> new line
#define IGNBRK 0x4    // ignoring break condition, translating into NULL byte
#define IGNCR  0x8    // ignore carriage return
#define IGNPAR 0x10   // ignore characters with parity error
#define INLCR  0x20   // new line -> carriage return
#define INPCK  0x40   // enable parity check
#define ISTRIP 0x80   // strip 8 bit ascii to 7 bit
#define IXANY  0x100  // any received character resumes output
#define IXOFF  0x200  // VSTOP/VSTART to pause/resume character input when internal buffer full? TODO: implement
#define IXON   0x400  // VSTOP/VSTART to pause/resume character output
#define PARMRK 0x800  // apparently 0xFF 0x00 <char> on parity error, in case char is supposed to be 0xFF, 0xFF 0xFF

#define OPOST  1 // whether to even do processing
#define ONLCR  2 // new line -> carriage return new line
#define OCRNL  4 // carriage return -> new line
#define ONOCR  8 // do not output \r on first column
#define ONLRET 16 // new line resets tty column counter to 0
//nldly, crdly, tabdly, bsdly, vtdly


#define CBAUD   0x1F
#define B0      0x0
#define B50     0x1
#define B75     0x2
#define B110    0x3
#define B134    0x4
#define B150    0x5
#define B200    0x6
#define B300    0x7
#define B600    0x8
#define B1200   0x9
#define B1800   0xA
#define B2400   0xB
#define B4800   0xC
#define B9600   0xD
#define B19200  0xE
#define B38400  0xF
#define B57600  0x10
#define B115200 0x11

#define  CSIZE 0x60   // character size mask
#define    CS5 0x00   // 5 bits per character
#define    CS6 0x20   // 6 bits per character
#define    CS7 0x40   // 7 bits per character
#define    CS8 0x60   // 8 bits per character
#define CSTOPB 0x80   // 2 stop bits
#define CREAD  0x100  // enable receiver
#define PARENB 0x200  // parity enable
#define PARODD 0x400  // odd parity, else even
#define HUPCL  0x800  // send hangup on last FD close
#define CLOCAL 0x1000 // ignore modem lines, partially implemented for hangup, TODO: hw flow control


#define ECHO    0x1
#define ECHOE   0x2   // do VERASE visually (as '\b \b') - the standard backspace behavior
#define ECHOK   0x4   // do VKILL visually - the standard CTRL+U behavior
#define ECHONL  0x8   // echo \n even if ECHO is disabled
#define ICANON  0x10  // newline buffered, control chars handling
#define ISIG    0x20  // enable signals
#define NOFLSH  0x40  // don't do flushing on VINTR, VQUIT
#define TOSTOP  0x80  // set SIGTTOU if background process group tries to write()
#define ECHOCTL 0x100 // echo escapes as ^X
#define IEXTEN  0x8000 // marks extended options are used (currently none)

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

// we do blocking output to ttys anyway, so this is just returns 0 as it would already be drained
int tcdrain(int fildes);

int tcsendbreak(int fildes, int duration);

// we (our hardware) don't support split baud, so ospeed does the same as ispeed
speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int cfsetispeed(struct termios *termios_p, speed_t speed);
int cfsetospeed(struct termios *termios_p, speed_t speed);
#endif