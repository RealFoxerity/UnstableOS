#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>


static const char * cc_names[NCCS] = {
    [VEOF]   = "eof",
    [VEOL]   = "eol",
    [VERASE] = "erase",
    [VINTR]  = "intr",
    [VKILL]  = "kill",
    [VMIN]   = "min",
    [VQUIT]  = "quit",
    [VSTART] = "start",
    [VSTOP]  = "stop",
    [VSUSP]  = "susp",
    [VTIME]  = "time",
};

struct flag_field {
    const char * name;
    tcflag_t mask;
};

#define TTYDEF_IFLAG    (ICRNL | ISTRIP | IXANY | IXON)
#define TTYDEF_OFLAG    (OPOST | ONLCR)
#define TTYDEF_LFLAG    (ECHO | ECHOE | ECHOK | ICANON | ISIG | ECHOCTL)
#define TTYDEF_CFLAG    (B115200 | CS8 | CREAD | HUPCL)

#define CTRL(c) ((c) & 0x1F)

const struct termios tty_default_settings = {
    .c_iflag = TTYDEF_IFLAG,
    .c_lflag = TTYDEF_LFLAG,
    .c_oflag = TTYDEF_OFLAG,
    .c_cflag = TTYDEF_CFLAG,
    .c_cc    = {
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
        [VTIME]  = 0
    },
};

struct flag_field input_flags[] = {
    [BRKINT] = {
        .name = "brkint",
        .mask = BRKINT,
    },
    [ICRNL] = {
        .name = "icrnl",
        .mask = ICRNL,
    },
    [IGNBRK] = {
        .name = "ignbrk",
        .mask = IGNBRK,
    },
    [IGNCR] = {
        .name = "igncr",
        .mask = IGNCR,
    },
    [IGNPAR] = {
        .name = "ignpar",
        .mask = IGNPAR,
    },
    [INLCR] = {
        .name = "inlcr",
        .mask = INLCR,
    },
    [INPCK] = {
        .name = "inpck",
        .mask = INPCK,
    },
    [ISTRIP] = {
        .name = "istrip",
        .mask = ISTRIP,
    },
    [IXANY] = {
        .name = "ixany",
        .mask = IXANY,
    },
    [IXOFF] = {
        .name = "ixoff",
        .mask = IXOFF,
    },
    [IXON] = {
        .name = "ixon",
        .mask = IXON,
    },
    [PARMRK] = {
        .name = "parmrk",
        .mask = PARMRK,
    },
};

struct flag_field output_flags[] = {
    [OPOST] = {
        .name = "opost",
        .mask = OPOST,
    },
    [ONLCR] = {
        .name = "onlcr",
        .mask = ONLCR,
    },
    [OCRNL] = {
        .name = "ocrnl",
        .mask = OCRNL,
    },
    [ONOCR] = {
        .name = "onocr",
        .mask = ONOCR,
    },
    [ONLRET] = {
        .name = "onlret",
        .mask = ONLRET,
    },
};

struct flag_field local_flags[] = {
    [ECHO] = {
        .name = "echo",
        .mask = ECHO,
    },
    [ECHOE] = {
        .name = "echoe",
        .mask = ECHOE,
    },
    [ECHOK] = {
        .name = "echok",
        .mask = ECHOK,
    },
    [ECHONL] = {
        .name = "echonl",
        .mask = ECHONL,
    },
    [ICANON] = {
        .name = "icanon",
        .mask = ICANON,
    },
    [ISIG] = {
        .name = "isig",
        .mask = ISIG,
    },
    [NOFLSH] = {
        .name = "noflsh",
        .mask = NOFLSH,
    },
    [TOSTOP] = {
        .name = "tostop",
        .mask = TOSTOP,
    },
    [ECHOCTL] = {
        .name = "echoctl",
        .mask = ECHOCTL,
    },
};

struct flag_field control_flags[] = {
    /*
    [CS5] = {
        .name = "cs5",
        .mask = CS5,
    },
    [CS6] = {
        .name = "cs6",
        .mask = CS6,
    },
    [CS7] = {
        .name = "cs7",
        .mask = CS7,
    },
    [CS8] = {
        .name = "cs8",
        .mask = CS8,
    },
    */
    [CSTOPB] = {
        .name = "cstopb",
        .mask = CSTOPB,
    },
    [CREAD] = {
        .name = "cread",
        .mask = CREAD,
    },
    [PARENB] = {
        .name = "parenb",
        .mask = PARENB,
    },
    [PARODD] = {
        .name = "parodd",
        .mask = PARODD,
    },
    [HUPCL] = {
        .name = "hupcl",
        .mask = HUPCL,
    },
    [CLOCAL] = {
        .name = "clocal",
        .mask = CLOCAL,
    },
};

// this is so atrocious :sob:
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

// simplified "visible" function from busybox so that the control char output looks the same
// https://elixir.bootlin.com/busybox/1.37.0/source/libbb/printable.c

void visible(unsigned char c, char * buf) {
    if (c >= 128) {
        c -= 128;
        *buf++ = 'M'; // "alt" + key
        *buf++ = '-';
    }
    if (c < ' ' || c == 0x7F) {
        *buf++ = '^';
        c ^= 0x40; // essentially switches a letter case
    }
    *buf++ = c;
    *buf = '\0';
}

void print_termios_structure(const struct termios * termios, char show_all) {
    if (cfgetispeed(termios) != cfgetospeed(termios))
        printf("ispeed %d baud; ospeed %d baud;\n",
            baud_lookup[cfgetispeed(termios) & CBAUD],
            baud_lookup[cfgetospeed(termios) & CBAUD]);
    else
        printf("speed %d baud;\n", baud_lookup[cfgetispeed(termios) & CBAUD]);

    char printed = 0;
    // control characters
    for (int i = 0; i < NCCS; i++) {
        if (termios->c_cc[i] == tty_default_settings.c_cc[i] && !show_all) continue;
        printed = 1;
        printf("%s = ", cc_names[i]);
        switch (i) {
            case VMIN:
            case VTIME:
                printf("%d", termios->c_cc[i]);
                break;
            default:
                if (termios->c_cc[i] == _POSIX_VDISABLE)
                    printf("<undef>");
                else {
                    char character_string[8] = {0};
                    visible(termios->c_cc[i], character_string);
                    printf("%s", character_string);
                }
        }
        printf(";");
        if (i != NCCS - 1) printf(" ");
    }
    if (printed) printf("\n");

    // control flags
    printed = 0;
    for (int i = 0; i < sizeof(control_flags)/sizeof(control_flags[0]); i++) {
        if (control_flags[i].name == NULL) continue;
        if ((termios->c_cflag             & control_flags[i].mask) ==
            (tty_default_settings.c_cflag & control_flags[i].mask) && !show_all) continue;

        printed = 1;

        if (!(termios->c_cflag & control_flags[i].mask))
            printf("-");

        printf("%s", control_flags[i].name);
        if (i != sizeof(control_flags)/sizeof(control_flags[0]) - 1) printf(" ");
    }
    tcflag_t cs = termios->c_cflag & CSIZE;
    if (cs != TTYDEF_CFLAG & CSIZE || show_all)
        printf("%scs%d\n", printed ? " " : "", (cs >> 5) + 5);
    else if (printed)
        printf("\n");

    // input flags
    printed = 0;
    for (int i = 0; i < sizeof(input_flags)/sizeof(input_flags[0]); i++) {
        if (input_flags[i].name == NULL) continue;
        if ((termios->c_iflag             & input_flags[i].mask) ==
            (tty_default_settings.c_iflag & input_flags[i].mask) && !show_all) continue;

        printed = 1;

        if (!(termios->c_iflag & input_flags[i].mask))
            printf("-");

        printf("%s", input_flags[i].name);
        if (i != sizeof(input_flags)/sizeof(input_flags[0]) - 1) printf(" ");
    }
    if (printed) printf("\n");

    // output flags
    printed = 0;
    for (int i = 0; i < sizeof(output_flags)/sizeof(output_flags[0]); i++) {
        if (output_flags[i].name == NULL) continue;
        if ((termios->c_oflag             & output_flags[i].mask) ==
            (tty_default_settings.c_oflag & output_flags[i].mask) && !show_all) continue;

        printed = 1;

        if (!(termios->c_oflag & output_flags[i].mask))
            printf("-");

        printf("%s", output_flags[i].name);
        if (i != sizeof(output_flags)/sizeof(output_flags[0]) - 1) printf(" ");
    }
    if (printed) printf("\n");

    // local flags
    printed = 0;
    for (int i = 0; i < sizeof(local_flags)/sizeof(local_flags[0]); i++) {
        if (local_flags[i].name == NULL) continue;
        if ((termios->c_lflag             & local_flags[i].mask) ==
            (tty_default_settings.c_lflag & local_flags[i].mask) && !show_all) continue;

        printed = 1;

        if (!(termios->c_lflag & local_flags[i].mask))
            printf("-");

        printf("%s", local_flags[i].name);
        if (i != sizeof(local_flags)/sizeof(local_flags[0]) - 1) printf(" ");
    }
    if (printed) printf("\n");
}

void show_help(const char * argv0) {
    printf(
        "Usage:\t%s [SETTING]...\n"
        "  or\t%s [-a]\n"
        "  or\t%s [-g]\n"
        "Set the options for a terminal\n"
        "-a\tPrint all current settings in human-readable format\n"
        "-g\tPrint all current settings in stty-readable format\n"
        "-F DEV\tAct on DEV instead of the controlling terminal\n"
        "--help\tDisplay this help message\n"
        "- before SETTING negates its effect\n\n"
        "Control characters\n"
        "\teof \tCHAR\tSends an end-of-file\n"
        "\teol \tCHAR\tSends an end-of-line\n"
        "\terase\tCHAR\tErases the last character typed\n"
        "\tintr\tCHAR\tRaises a SIGINT to the foreground process group\n"
        "\tkill\tCHAR\tErases the current line\n"
        "\tquit\tCHAR\tRaises a SIGQUIT to the foreground process group\n"
        "\tstart\tCHAR\tRestarts stopped output\n"
        "\tstop\tCHAR\tStops output\n"
        "\tsusp\tCHAR\tRaises a SIGTSTP to the foreground process group\n\n"
        "Special settings\n"
        "\tN   \t\tSets both the input and output speed to N baud, 0 to hangup\n"
        "\tispeed\tN\tSets the input speed to N baud, 0 to hangup\n"
        "\tospeed\tN\tSets the output speed to N baud, 0 to hangup\n"
        "\tspeed\t\tPrints the terminal speed, 0 if hungup\n"
        "\tmin \tN\tIn non-canonical mode specifies minimum N bytes to read\n"
        "\ttime\tN\tIn non-canonical mode specifies read timeout in deciseconds\n\n"
        "Control settings\n"
        "\t[-]clocal\tDisables modem control (HUP, HW flow)\n"
        "\t[-]cread\tAllows receiving input\n"
        "\tcsN     \tSets character size to N (5-8) bits\n"
        "\t[-]cstopb\tSets 2 stop bits\n"
        "\t[-]hup  \tSend hangup on last fd close\n"
        "\t[-]hupcl\tSame as hup\n"
        "\t[-]parenb\tEnables parity\n"
        "\t[-]parodd\tSets odd parity\n\n"
        "Input settings\n"
        "\t[-]brkint\tBreaks raise SIGINT\n"
        "\t[-]icrnl\tTranslates \\r into \\n\n"
        "\t[-]ignbrk\tIgnore breaks\n"
        "\t[-]igncr\tIgnore \\r\n"
        "\t[-]ignpar\tIgnore characters with parity errors\n"
        "\t[-]inlcr\tTranslates \\n into \\r\n"
        "\t[-]inpck\tEnables parity check\n"
        "\t[-]istrip\tStrips high bit from each character\n"
        "\t[-]ixany\tAny recieved character resumes stopped output\n"
        "\t[-]ixoff\tEnables sending VSTART/VSTOP on full internal buffer\n"
        "\t[-]ixon \tEnables VSTART/VSTOP output flow control\n"
        "\t[-]parmrk\tEnables marking characters with parity errors by 0xFF 0x00 <c>\n\n"
        "Output settings\n"
        "\t[-]opost\tEnables output postprocessing\n"
        "\t[-]onlcr\tTranslates \\n to \\r\\n\n"
        "\t[-]ocrnl\tTranslates \\r to \\n\n"
        "\t[-]onocr\tDo not output \\r on first column\n"
        "\t[-]onlret\t\\n performs \\r action (internally)\n\n"
        "Local settings\n"
        "\t[-]echo \tEcho input characters\n"
        "\t[-]echoe\tEcho VERASE as '\\b \\b'\n"
        "\t[-]echok\tEcho a \\n after VKILL\n"
        "\t[-]echonl\tEcho a \\n even if not normally echoing\n"
        "\t[-]icanon\tEnable line buffering and control characters VEOL, VEOF, VERASE, and VKILL\n"
        "\t[-]isig  \tEnable control characters VINTR, VQUIT, and VSUSP\n"
        "\t[-]noflsh\tDisable flushing on VINTR, and VQUIT\n"
        "\t[-]tostop\tWrite() from background process group raises SIGTTOU\n"
        "\t[-]echoctl\tEcho control characters (< 0x20) using ^ notation\n\n"
        "Combination settings\n"
        "\tevenp \tSame as parenb cs7 -parodd\n"
        "\tparity\tSame as evenp\n"
        "\toddp  \tSame as parenb cs7 parodd\n"
        "\t-parity\tSame as -parenb cs8\n"
        "\t-evenp\tSame as -parity\n"
        "\t-oddp\tSame as -parity\n"
        "\tcooked\tSame as:\n"
        "\t      \ticrnl istrip ixany ixon\n"
        "\t      \topost\n"
        "\t      \ticanon isig\n"
        "\t      \teof ^D eol ^@ erase ^? intr ^C quit ^\\ kill ^U\n"
        "\t-cooked\tSame as raw\n"
        "\tek  \tSame as erase ^? kill ^U\n"
        "\tnl  \tSame as -icrnl\n"
        "\t-nl \tSame as icrnl -inlcr -igncr\n"
        "\traw \tSame as:\n"
        "\t    \t-brkint -icrnl -ignbrk -igncr -ignpar -inlcr -inpck -strip -ixany -ixoff -ixon\n"
        "\t    \t-opost\n"
        "\t    \t-icanon -isig\n"
        "\t    \tmin 1 min 0 eof ^- eol ^- erase ^- intr ^- quit ^- kill ^-\n"
        "\t-raw\tSame as cooked\n"
        "\tsane\tSame as:\n"
        "\t    \t115200 -clocal cread cs8 -cstopb hup -parenb\n"
        "\t    \t-brkint icrnl -ignbrk -igncr -ignpar -inlcr -inpck istrip ixany ixon\n"
        "\t    \topost onlcr -ocrnl -onocr -onlret echo echoe echok -echonl icanon isig -noflsh echoctl\n"
        "\t    \teof ^D eol ^@ erase ^? intr ^C kill ^U min 1 quit ^\\ start ^Q stop ^S susp ^Z time 0\n"
    , argv0, argv0, argv0);
}

int parse_special_char(char * arg) {
    if (arg == NULL) return -1;
    if (strlen(arg) == 0) return -1;

    if (strlen(arg) == 1) return arg[0];
    if (strcmp("^-", arg) == 0 || strcmp("undef", arg) == 0)
        return _POSIX_VDISABLE;
    if (strcmp("^?", arg) == 0)
        return 0x7F;
    if (arg[0] == '^')    return CTRL(arg[1]);

    errno = 0;
    long val = strtol(arg, NULL, 0);
    if (errno != 0) return -1;
    if (val < 0 || val > 255) return -2;
    return val;
}

char parse_modeline(const char * modeline, struct termios * out) {
    unsigned int value = 0;
    char * end = NULL;
    value = strtoul(modeline, &end, 16);
    if (modeline == end) return 0;
    if (*end != ':') return 0;
    modeline = end + 1;
    out->c_iflag = value;

    value = strtoul(modeline, &end, 16);
    if (modeline == end) return 0;
    if (*end != ':') return 0;
    modeline = end + 1;
    out->c_oflag = value;

    value = strtoul(modeline, &end, 16);
    if (modeline == end) return 0;
    if (*end != ':') return 0;
    modeline = end + 1;
    out->c_lflag = value;

    value = strtoul(modeline, &end, 16);
    if (modeline == end) return 0;
    if (*end != ':') return 0;
    modeline = end + 1;
    out->c_cflag = value;

    for (int i = 0; i < NCCS - 1; i++) {
        value = strtoul(modeline, &end, 16);
        if (value > 255 || modeline == end) return 0;
        if (*end != ':') return 0;
        modeline = end + 1;
        out->c_cc[i] = value;
    }

    value = strtoul(modeline, &end, 16);
    if (value > 255 || modeline == end) return 0;
    if (*end != '\0') return 0;
    out->c_cc[NCCS-1] = value;

    return 1;
}

int main(int argc, char *argv[]) {
    int fd = STDIN_FILENO;

    char found_term = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-F") == 0) {
            if (found_term) {
                fprintf(stderr, "stty: multiple -F arguments\n");
                fprintf(stderr, "'stty --help' for more information.\n");
                return 1;
            }
            if (!argv[i+1]) {
                fprintf(stderr, "stty: missing argument for -F\n");
                fprintf(stderr, "'stty --help' for more information.\n");
                return 1;
            }
            found_term = 1;
            fd = open(argv[i+1], O_RDWR | O_NOCTTY);
            if (fd == -1) {
                perror("stty: terminal");
                return 1;
            }
        }
    }

    if (!isatty(fd)) {
        perror("stty: terminal");
        return 1;
    }

    struct termios expected;
    if (tcgetattr(fd, &expected) != 0) {
        perror("tcgetattr");
        return 255;
    }

    if (argc == 1) {
        print_termios_structure(&expected, 0);
        return 0;
    }

    char show_all = 0;
    char show_modeline = 0;
    char show_speed = 0;
    char actual_options = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-F") == 0) {
            i++;
            continue;
        }
        if (strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            return 0;
        }
        if (show_all || show_modeline) {
            if (show_all && show_modeline)
                fprintf(stderr, "stty: the options for verbose and stty-readable output styles are mutually exclusive\n");
            else
                fprintf(stderr, "stty: when specifying an output style, modes may not be set\n");
            return 3;
        }
        if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
            continue;
        }
        if (strcmp(argv[i], "-g") == 0) {
            show_modeline = 1;
            continue;
        }
        if (strcmp(argv[0], "speed") == 0) {
            show_speed = 1;
            continue;
        }
        actual_options = 1;

        char op_is_neg = argv[i][0] == '-' ? 1 : 0;

        // do combinations first
        // missing evenp, parity, oddp
        if (strcmp(argv[i] + op_is_neg, "evenp") == 0 || strcmp(argv[i] + op_is_neg, "parity") == 0) {
            if (op_is_neg) {
                disable_parity:
                expected.c_cflag &= ~PARENB;
                expected.c_cflag &= ~CSIZE;
                expected.c_cflag |= CS8;
                continue;
            }
            expected.c_cflag |= PARENB;
            expected.c_cflag &= ~PARODD;
            expected.c_cflag &= ~CSIZE;
            expected.c_cflag |= CS7;
            continue;
        }
        if (strcmp(argv[i] + op_is_neg, "oddp") == 0) {
            if (op_is_neg)
                goto disable_parity;
            expected.c_cflag |= PARENB;
            expected.c_cflag |= PARODD;
            expected.c_cflag &= ~CSIZE;
            expected.c_cflag |= CS7;
            continue;
        }
        if (strcmp(argv[i], "cooked") == 0) goto set_cooked;
        if (strcmp(argv[i], "-cooked") == 0) goto set_raw;
        if (strcmp(argv[i] + op_is_neg, "raw") == 0) {
            if (op_is_neg) {
                set_cooked:
                expected.c_cc[VEOF]   = tty_default_settings.c_cc[VEOF];
                expected.c_cc[VEOL]   = tty_default_settings.c_cc[VEOL];
                expected.c_cc[VERASE] = tty_default_settings.c_cc[VERASE];
                expected.c_cc[VINTR]  = tty_default_settings.c_cc[VINTR];
                expected.c_cc[VQUIT]  = tty_default_settings.c_cc[VQUIT];
                expected.c_cc[VKILL]  = tty_default_settings.c_cc[VKILL];

                expected.c_iflag     |= tty_default_settings.c_iflag;
                expected.c_oflag     |= OPOST;
                expected.c_lflag     |= ICANON | ISIG;
                continue;
            }
            set_raw:
            expected.c_cc[VEOF]   = _POSIX_VDISABLE;
            expected.c_cc[VEOL]   = _POSIX_VDISABLE;
            expected.c_cc[VERASE] = _POSIX_VDISABLE;
            expected.c_cc[VINTR]  = _POSIX_VDISABLE;
            expected.c_cc[VQUIT]  = _POSIX_VDISABLE;
            expected.c_cc[VKILL]  = _POSIX_VDISABLE;

            expected.c_iflag      = 0;
            expected.c_oflag     &= ~OPOST;
            expected.c_lflag     &= ~(ICANON | ISIG);
            expected.c_cc[VMIN]   = 1;
            expected.c_cc[VTIME]  = 0;
            continue;
        }
        if (strcmp(argv[i] + op_is_neg, "nl") == 0) {
            if (op_is_neg) {
                expected.c_iflag |= ICRNL;
                expected.c_iflag &= ~INLCR;
                expected.c_iflag &= ~IGNCR;
            } else
                expected.c_iflag &= ~ICRNL;
            continue;
        }
        if (strcmp(argv[i], "ek") == 0) {
            expected.c_cc[VERASE] = tty_default_settings.c_cc[VERASE];
            expected.c_cc[VKILL]  = tty_default_settings.c_cc[VKILL];
            continue;
        }
        if (strcmp(argv[i], "sane") == 0) {
            memcpy(&expected, &tty_default_settings, sizeof(struct termios));
            continue;
        }

        // input flags
        char found_flag = 0;
        for (int j = 0; j < sizeof(input_flags)/sizeof(input_flags[0]); j++) {
            if (input_flags[j].name != NULL && strcmp(input_flags[j].name, argv[i] + op_is_neg) == 0) {
                if (op_is_neg)
                    expected.c_iflag &= ~input_flags[j].mask;
                else
                    expected.c_iflag |= input_flags[j].mask;
                found_flag = 1;
                break;
            }
        }
        if (found_flag) continue;

        // output flags
        found_flag = 0;
        for (int j = 0; j < sizeof(output_flags)/sizeof(output_flags[0]); j++) {
            if (output_flags[j].name != NULL && strcmp(output_flags[j].name, argv[i] + op_is_neg) == 0) {
                if (op_is_neg)
                    expected.c_oflag &= ~output_flags[j].mask;
                else
                    expected.c_oflag |= output_flags[j].mask;
                found_flag = 1;
                break;
            }
        }
        if (found_flag) continue;

        // local flags
        found_flag = 0;
        for (int j = 0; j < sizeof(local_flags)/sizeof(local_flags[0]); j++) {
            if (local_flags[j].name != NULL && strcmp(local_flags[j].name, argv[i] + op_is_neg) == 0) {
                if (op_is_neg)
                    expected.c_lflag &= ~local_flags[j].mask;
                else
                    expected.c_lflag |= local_flags[j].mask;
                found_flag = 1;
                break;
            }
        }
        if (found_flag) continue;

        // control flags
        found_flag = 0;
        for (int j = 0; j < sizeof(control_flags)/sizeof(control_flags[0]); j++) {
            if (control_flags[j].name != NULL && strcmp(control_flags[j].name, argv[i] + op_is_neg) == 0) {
                if (op_is_neg)
                    expected.c_cflag &= ~control_flags[j].mask;
                else
                    expected.c_cflag |= control_flags[j].mask;
                found_flag = 1;
                break;
            }
        }
        if (found_flag) continue;
        // special chars
        found_flag = 0;
        if (op_is_neg) goto errored;
        for (int j = 0; j < sizeof(cc_names)/sizeof(cc_names[0]); j++) {
            if (cc_names[j] != NULL && strcmp(cc_names[j], argv[i]) == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "stty: missing argument to '%s'\n", argv[i]);
                    fprintf(stderr, "'stty --help' for more information.\n");
                    return 1;
                }

                int cc = -1;
                if (j != VMIN && j != VTIME) {
                    cc = parse_special_char(argv[++i]);
                } else {
                    errno = 0;
                    cc = strtol(argv[++i], NULL, 0);
                    if (errno != 0) cc = -1;
                }
                if (cc == -1) {
                    fprintf(stderr, "stty: invalid integer argument: '%s'\n", argv[i]);
                    return 1;
                }
                if (cc == -2) {
                    fprintf(stderr,
                        "stty: invalid integer argument: '%s': Value too large to be a control char\n", argv[i]
                    );
                    return 1;
                }
                expected.c_cc[j] = cc;
                found_flag = 1;
                break;
            }
        }
        if (found_flag) continue;

        if (parse_modeline(argv[i], &expected)) continue;

        // some cflags can't be represented by the bitfields we usually use, so last chance for those
        if (strcmp(argv[i], "cs5") == 0) {
            expected.c_cflag &= ~CSIZE;
            expected.c_cflag |= CS5;
            continue;
        }
        if (strcmp(argv[i], "cs6") == 0) {
            expected.c_cflag &= ~CSIZE;
            expected.c_cflag |= CS6;
            continue;
        }
        if (strcmp(argv[i], "cs7") == 0) {
            expected.c_cflag &= ~CSIZE;
            expected.c_cflag |= CS7;
            continue;
        }
        if (strcmp(argv[i], "cs8") == 0) {
            expected.c_cflag &= ~CSIZE;
            expected.c_cflag |= CS8;
            continue;
        }

        if (strcmp(argv[i], "ispeed") == 0) {
            if (!argv[++i]) {
                fprintf(stderr, "stty: missing argument to 'ispeed'\n");
                fprintf(stderr, "'stty --help' for more information.\n");
                return 1;
            }
            char * end = NULL;
            if (argv[i][0] == '-')
                goto errored;
            speed_t ispeed = strtol(argv[i], &end, 10);
            if (!end || *end != '\0')
                goto errored;
            int j = 0;
            for (j = 0; j < CBAUD + 1; j++) {
                if (ispeed == baud_lookup[j]) {
                    cfsetispeed(&expected, j);
                    break;
                }
            }
            if (j != CBAUD + 1)
                continue;
            fprintf(stderr, "stty: invalid input baud rate: %d\n", ispeed);
            fprintf(stderr, "'stty --help' for more information.\n");
            return 1;
        }
        if (strcmp(argv[i], "ospeed") == 0) {
            if (!argv[++i]) {
                fprintf(stderr, "stty: missing argument to 'ospeed'\n");
                fprintf(stderr, "'stty --help' for more information.\n");
                return 1;
            }
            char * end = NULL;
            if (argv[i][0] == '-')
                goto errored;
            speed_t ospeed = strtol(argv[i], &end, 10);
            if (!end || *end != '\0')
                goto errored;
            int j = 0;
            for (j = 0; j < CBAUD + 1; j++) {
                if (ospeed == baud_lookup[j]) {
                    cfsetospeed(&expected, j);
                    break;
                }
            }
            if (j != CBAUD + 1)
                continue;
            fprintf(stderr, "stty: invalid output baud rate: %d\n", ospeed);
            fprintf(stderr, "'stty --help' for more information.\n");
            return 1;
        }

        char * end = NULL;
        if (argv[i][0] == '-')
            goto errored;
        speed_t speed = strtol(argv[i], &end, 10);
        if (!end || *end != '\0')
            goto errored;
        int j = 0;
        for (j = 0; j < CBAUD + 1; j++) {
            if (speed == baud_lookup[j]) {
                cfsetispeed(&expected, j);
                cfsetospeed(&expected, j);
                break;
            }
        }
        if (j != CBAUD + 1)
            continue;
        fprintf(stderr, "stty: invalid baud rate: %d\n", speed);
        fprintf(stderr, "'stty --help' for more information.\n");
        return 1;

        errored:
        fprintf(stderr, "stty: invalid argument '%s'\n", argv[i]);
        fprintf(stderr, "'stty --help' for more information.\n");
        return 1;
    }

    if (!actual_options) {
        print_termios_structure(&expected, show_all);
        return 0;
    }
    if (show_modeline) {
        printf("%x:%x:%x:%x:", expected.c_iflag, expected.c_oflag, expected.c_lflag, expected.c_cflag);
        for (int i = 0; i < NCCS - 1; i++) {
            printf("%hhx:", expected.c_cc[i]);
        }
        printf("%hhx\n", expected.c_cc[NCCS - 1]);
        return 0;
    }
    if (tcsetattr(fd, TCSANOW, &expected) != 0) {
        perror("stty: tcsetattr");
        return 255;
    }

    struct termios actual;
    if (tcgetattr(fd, &actual) != 0) {
        perror("stty: tcgetattr");
        return 255;
    }
    if (memcmp(&actual, &expected, sizeof(struct termios)) != 0) {
        fprintf(stderr, "stty: standard input: unable to perform all requested operations\n");
        return 1;
    }
    if (show_speed) {
        printf("%d\n", cfgetispeed(&expected));
    }
    return 0;
}