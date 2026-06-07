#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stddef.h>

#include <termios.h>

#include <stdlib.h>

#include "string.h"

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

#define CTRL(c) ((c) & 0x1F)

const struct termios tty_default_settings = {
    .c_iflag = TTYDEF_IFLAG,
    .c_lflag = TTYDEF_LFLAG,
    .c_oflag = TTYDEF_OFLAG,
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
    // --

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
        "--help\tDisplay this help message\n"
        "- before SETTING negates its effect\n\n"
        "Control characters\n"
        "\teof\tCHAR\tSends an end-of-file\n"
        "\teol\tCHAR\tSends an end-of-line\n"
        "\terase\tCHAR\tErases the last character typed\n"
        "\tintr\tCHAR\tRaises a SIGINT to the foreground process group\n"
        "\tkill\tCHAR\tErases the current line\n"
        "\tquit\tCHAR\tRaises a SIGQUIT to the foreground process group\n"
        "\tstart\tCHAR\tRestarts stopped output\n"
        "\tstop\tCHAR\tStops output\n"
        "\tsusp\tCHAR\tRaises a SIGTSTP to the foreground process group\n\n"
        "Special settings\n"
        "\tmin\tN\tIn non-canonical mode specifies minimum N bytes to read\n"
        "\ttime\tN\tIn non-canonical mode specifies read timeout in deciseconds\n\n"
        "Control settings\n"
        "\t* currently none supported\n\n"
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
        "\t[-]ixon \tEnables VSTART/VSTOP output flow control\n\n"
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
        "\tcooked\tSame as icrnl istrip ixany ixon opost icanon isig eof ^D eol ^@ erase ^?\n"
        "\t\tintr ^C quit ^\\ kill ^U\n"
        "\t-cooked\tSame as raw\n"
        "\tek\tSame as erase ^? kill ^U\n"
        "\tnl\tSame as -icrnl\n"
        "\t-nl\tSame as icrnl -inlcr -igncr\n"
        "\traw\tSame as -brkint -icrnl -ignbrk -igncr -ignpar -inlcr -inpck -strip -ixany -ixoff\n"
        "\t\t-ixon -opost -icanon -isig min 1 min 0 eof ^- eol ^- erase ^- intr ^- quit ^- kill ^-\n"
        "\t-raw\tSame as cooked\n"
        "\tsane\tSame as -brkint icrnl -ignbrk -igncr -ignpar -inlcr -inpck istrip ixany ixon\n"
        "\t\topost onlcr -ocrnl -onocr -onlret echo echoe echok -echonl icanon isig -noflsh echoctl\n"
        "\t\teof ^D eol ^@ erase ^? intr ^C kill ^U min 1 quit ^\\ start ^Q stop ^S susp ^Z time 0\n"
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
    if (!isatty(STDIN_FILENO)) {
        perror("stty: standard input");
        return 1;
    }

    struct termios expected;
    if (tcgetattr(STDIN_FILENO, &expected) != 0) {
        perror("tcgetattr");
        return 255;
    }

    if (argc == 1) {
        print_termios_structure(&expected, 0);
        return 0;
    }

    char show_all = 0;
    char show_modeline = 0;

    for (int i = 1; i < argc; i++) {
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
        if (strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            return 0;
        }
        char op_is_neg = argv[i][0] == '-' ? 1 : 0;

        // do combinations first
        // missing evenp, parity, oddp
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

        // missing all control flags and baud rates as they are not supported in the kernel

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
                    fprintf(stderr, "ssty: invalid integer argument: '%s'\n", argv[i]);
                    return 1;
                }
                if (cc == -2) {
                    fprintf(stderr,
                        "ssty: invalid integer argument: '%s': Value too large to be a control char\n", argv[i]
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

        errored:
        fprintf(stderr, "stty: invalid argument '%s'\n", argv[i]);
        fprintf(stderr, "'stty --help' for more information.\n");
        return 1;
    }

    if (show_all) {
        print_termios_structure(&expected, 1);
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
    if (tcsetattr(STDIN_FILENO, TCSANOW, &expected) != 0) {
        perror("stty: tcsetattr");
        return 255;
    }

    struct termios actual;
    if (tcgetattr(STDIN_FILENO, &actual) != 0) {
        perror("stty: tcgetattr");
        return 255;
    }
    if (memcmp(&actual, &expected, sizeof(struct termios)) != 0) {
        fprintf(stderr, "stty: standard input: unable to perform all requested operations\n");
        return 1;
    }

    return 0;
}