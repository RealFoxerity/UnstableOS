#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/utsname.h>
#include <termios.h>

#define F_OSREL "/etc/os-release"

extern struct termios term;
extern const int baud_lookup[CBAUD + 1];

FILE * osrel = NULL;
const char * get_osrel_var(const char * name) {
    if (!osrel)
        return NULL;

    rewind(osrel);

    static char * line = NULL;
    static size_t len = 0;

    ssize_t read_bytes;
    while ((read_bytes = getline(&line, &len, osrel)) > 0) {
        if (line[read_bytes - 1] == '\n')
            line[--read_bytes] = '\0';
        char * eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        if (strcmp(line, name) == 0) {
            // TODO: proper quoting, this is good enough for now
            if ((*(eq+1) == '"'  && line[read_bytes-1] == '"') ||
                (*(eq+1) == '\'' && line[read_bytes-1] == '\''))
                *++eq = line[read_bytes-1] = '\0';
            return eq + 1;
        }
    }
    return NULL;
}
struct utsname name = {0};
const char * get_var_name(FILE * issue, const char * fallback) {
    static char buf[32 + 1] = {0};
    int c = fgetc(issue);
    if (c != '{') {
        if (c == EOF)
            return NULL;

        ungetc(c, issue);
        if (!fallback)
            return NULL;
        strcpy(buf, fallback);
        return buf;
    }

    size_t off = 0;
    while ((c = fgetc(issue)) != EOF) {
        if (off >= 32)
            return NULL;
        if (isspace(c))
            return NULL;
        if (c == '}')
            break;
        buf[off++] = (char)c;
    }
    if (c == EOF)
        return NULL;
    buf[off] = 0;
    return buf;
}


#define ESCS \
    X("black", "\e[30m") \
    X("blue", "\e[34m") \
    X("bold", "\e[1m") \
    X("cyan", "\e[36m") \
    X("darkgray", "\e[90m") \
    X("darkgrey", "\e[90m") \
    X("green", "\e[32m") \
    X("lightblue", "\e[94m") \
    X("lightcyan", "\e[96m") \
    X("lightgreen", "\e[92m") \
    X("lightmagenta", "\e[95m") \
    X("lightred", "\e[91m") \
    X("lightyellow", "\e[93m") \
    X("magenta", "\e[35m") \
    X("red", "\e[31m") \
    X("reset", "\e[0m") \
    X("reverse", "\e[7m") \
    X("yellow", "\e[33m") \
    X("white", "\e[37m")

#define X(a, b) {\
    if (strcmp(name, a) == 0)\
        printf("%s", b);\
}
void print_readable_escape(FILE * issue) {
    const char * name = get_var_name(issue, "\e");
    if (!name)
        return;
    ESCS
}

void print_osrel_var(FILE * issue) {
    const char * name = get_var_name(issue, "PRETTY_NAME");
    if (!name)
        return;
    const char * val = get_osrel_var(name);
    if (!val)
        return;
    if (!strcmp(name, "ANSI_COLOR"))
        printf("\e[");
    printf("%s", val);
    if (!strcmp(name, "ANSI_COLOR"))
        printf("m");
}

void handle_issue_fmt(FILE * issue) {
    if (!issue)
        return;
    int c = fgetc(issue);
    if (c == EOF)
        return;
    switch (c) {
        case 'b':
            printf("%d", baud_lookup[cfgetispeed(&term) & CBAUD]);
            break;
        case 'd': {
            char buf[128];
            strftime(buf, 128, "%A, %d %B %Y", localtime(&(time_t){time(NULL)}));
            printf("%s", buf);
            break;
        }
        case 'e':
            print_readable_escape(issue);
            break;
        case 's':
            printf("%s", name.sysname);
            break;
        case 'S':
            print_osrel_var(issue);
            break;
        case 'l': {
            const char * tty = ttyname(STDIN_FILENO);
            if (tty)
                printf("%s", tty);
            break;
        }
        case 'm':
            printf("%s", name.machine);
            break;
        case 'n':
            printf("%s", name.nodename);
            break;
        case 'r':
            printf("%s", name.release);
            break;
        case 't': {
            char buf[128];
            strftime(buf, 128, "%H:%M:%S", localtime(&(time_t){time(NULL)}));
            printf("%s", buf);
            break;
        }
        default:
            putchar(c);
    }
}

void print_issue(int fd) {
    if (fd < 0)
        return;
    FILE * issue = fdopen(fd, "r");
    if (!issue)
        return;
    rewind(issue);

    osrel = fopen(F_OSREL, "r");

    uname(&name);

    int c;
    while ((c = fgetc(issue)) != EOF) {
        if (c == '\\')
            handle_issue_fmt(issue);
        else
            putchar(c);
    }
    fflush(stdout);
}