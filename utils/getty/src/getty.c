#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <errno.h>
#include <ctype.h>
#include <sys/ioctl.h>

#define DEFAULT_ISSUE "/etc/issue"
#define DEFAULT_LOGIN "/bin/login"

char hostname[HOST_NAME_MAX+1];

#define CTRL(x) ((x) & 0x1F)
struct termios orig;

#define KEPT_CFLAGS (CBAUD | CSIZE | CSTOPB | PARENB | PARODD)
struct termios term = {
    .c_iflag = ICRNL | IXANY | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | ECHOCTL,
    .c_cflag = CREAD | HUPCL,

    .c_cc = {
        [VEOF]   = CTRL('D'),
        [VEOL]   = '\x00',
        [VERASE] = '\x7f',
        [VINTR]  = CTRL('C'),
        [VKILL]  = CTRL('U'),
        [VMIN]   = 1,
        [VQUIT]  = CTRL('\\'),
        [VSTART] = CTRL('Q'),
        [VSTOP]  = CTRL('S'),
        [VSUSP]  = CTRL('Z'),
        [VTIME]  = 0,
    }
};
const int baud_lookup[CBAUD + 1] = {
    [B0]      = 0,
    [B50]     = 50,
    [B75]     = 75,
    [B110]    = 110,
    [B134]    = 134,
    [B150]    = 150,
    [B200]    = 200,
    [B300]    = 300,
    [B600]    = 600,
    [B1200]   = 1200,
    [B1800]   = 1800,
    [B2400]   = 2400,
    [B4800]   = 4800,
    [B9600]   = 9600,
    [B19200]  = 19200,
    [B38400]  = 38400,
    [B57600]  = 57600,
    [B115200] = 115200,
};
speed_t read_baud(const char * baud) {
    char * end;
    speed_t speed = strtol(baud, &end, 10);
    if (!end || *end)
        return (speed_t)-1;
    int j = 0;
    for (j = 0; j < CBAUD + 1; j++) {
        if (speed == baud_lookup[j])
            return j;
    }
    return (speed_t)-1;
}
// keep the same as login.c
// why?
// getty doesn't time out because reasons and passed the username to login
// and I decided to keep with the convention
void read_username(char ** username, size_t * len) {
    tcflush(STDIN_FILENO, TCIFLUSH);
    fflush(stdin);

    int c;
    again:
    printf("%s login: ", hostname);
    fflush(stdout);
    do {
        if ((c = getchar()) == EOF)
            exit(EXIT_FAILURE);
        if (c == '\n') {
            goto again;
        }
    } while (isspace(c));
    ungetc(c, stdin);
    int read_bytes = getline(username, len, stdin);
    if (read_bytes < 0)
        exit(EXIT_FAILURE);
    if (read_bytes && (*username)[read_bytes - 1] == '\n')
        (*username)[read_bytes - 1] = '\0';
}

// TODO: do getopt on every single argv entry so that options can be anywhere
static const char * valid_options =
"L" // CLOCAL
"n" // don't prompt for name
"w" // wait on cr/lf before sending /etc/issue
"i" // don't display /etc/issue
"f:"// use a different issue file
"l:"// use a different login program
"t:"// name timeout
;

static const char * help_message =
"Usage: getty [-Lnwi] [-f file] [-l login] [-t sec] baudrate tty\n\n"
"Configures a tty, prompts for login name, invokes login\n\n"
"    -L       Set CLOCAL (ignore hardware hangups)\n"
"    -n       Don't prompt for login name\n"
"    -w       Wait on CR/LF before displaying the issue file\n"
"    -i       Don't display the issue file\n"
"    -f file  Use an issue file other than "DEFAULT_ISSUE"\n"
"    -l login Use a login utility other than "DEFAULT_LOGIN"\n"
"    -t sec   Timeout on login name after sec\n\n"
"Baudrate 0 uses the currently configured one\n"
"Providing '-' as tty will use currently opened stdin\n";

__attribute__((noreturn)) void help() {
    fprintf(stderr, help_message);
    exit(EXIT_FAILURE);
}

int main(int argc, char ** argv) {
    if (argc < 3)
        help();

    struct {
        unsigned char local : 1;
        unsigned char noprompt : 1;
        unsigned char wait : 1;
        unsigned char noissue : 1;
    } opts = {0};
    const char * issue_file = DEFAULT_ISSUE;
    const char * login_util = DEFAULT_LOGIN;
    unsigned timeout_secs = 0;
    speed_t baud = B0;
    const char * tty = NULL;
    int c;
    while ((c = getopt(argc, argv, valid_options)) != -1) {
        switch (c) {
            case 'L':
                opts.local = 1;
                break;
            case 'n':
                opts.noprompt = 1;
                break;
            case 'w':
                opts.wait = 1;
                break;
            case 'i':
                opts.noissue = 1;
                break;
            case 'f':
                issue_file = optarg;
                break;
            case 'l':
                login_util = optarg;
                break;
            case 't': {
                char * end;
                timeout_secs = strtoul(optarg, &end, 10);
                if (!end || *end) {
                    fprintf(stderr, "getty: Invalid timeout specifier '%s'\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            }
            default:
                exit(EXIT_FAILURE);
        }
    }

    if (argc - optind != 2)
        help();
    baud = read_baud(argv[optind]);
    if (baud == (speed_t)-1) {
        fprintf(stderr, "getty: Invalid baud specifier '%s'\n", argv[optind]);
        exit(EXIT_FAILURE);
    }
    tty = argv[optind+1];
    if (strcmp(tty, "-") == 0) {
        // the old fd could possibly be reused, but why bother
        tty = ttyname(STDIN_FILENO);
        if (!tty) {
            fprintf(stderr, "getty: ttyname(0): %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    int issue_fd = open(issue_file, O_RDONLY | O_CLOEXEC);
    if (issue_fd < 0)
        fprintf(stderr, "getty: Warning: can't open issue file\n");
    else {
        dup2(issue_fd, 101); // so we don't accidentally close it below in case it's 0-2
        issue_fd = 101;
    }
    if (gethostname(hostname, HOST_NAME_MAX + 1) < 0)
        strcpy(hostname, "What hostname?");

    pid_t us = setsid();
    if (us < 0 && getsid(0) != getpid()) {
        // not already a leader, probably because we have processes in our pgrp
        fprintf(stderr, "getty: setsid(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // close current controlling terminal so that open() gives us one
    signal(SIGHUP, SIG_IGN);
    ioctl(STDIN_FILENO, TIOCNOTTY);
    signal(SIGHUP, SIG_DFL);

    int tty_fd = open(tty, O_RDWR);
    if (tty_fd < 0) {
        fprintf(stderr, "getty: open(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    dup2(tty_fd, 100); // so we don't accidentally close it
    close(tty_fd);
    tty_fd = 100;

    if (tcgetattr(tty_fd, &orig) < 0) {
        fprintf(stderr, "getty: tcgetattr(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    dup2(tty_fd, STDIN_FILENO);
    dup2(tty_fd, STDOUT_FILENO);
    dup2(tty_fd, STDERR_FILENO);

    close(tty_fd);

    term.c_cflag |= orig.c_cflag & KEPT_CFLAGS;
    cfsetospeed(&term, baud == 0 ? cfgetospeed(&orig) : baud);

    // probably (hopefully) doesn't matter if it fails
    tcsetattr(STDIN_FILENO, TCSANOW, &term);

    fflush(stdin);

    alarm(timeout_secs);
    printf("\e[H\e[2J\e[3J");
    fflush(stdout);

    if (opts.wait) {
        while ((c = getchar()) != EOF) {
            if (c == '\r' || c == '\n')
                break;
        }
    }

    if (!opts.noissue && issue_fd >= 0) {
        char copybuf[1024];
        size_t read_bytes;
        while ((read_bytes = read(issue_fd, copybuf, sizeof(copybuf))) > 0) {
            fwrite(copybuf, 1, read_bytes, stdout);
        }
        close(issue_fd);
    }

    char * login_name = NULL;
    size_t login_len;

    read_username(&login_name, &login_len);

    execlp(login_util, login_util, login_name, NULL);
    fprintf(stderr, "getty: execlp(): %s\n", strerror(errno));
    return 127;
}