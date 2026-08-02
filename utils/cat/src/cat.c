#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#define CAT_BUFFER 4096

char number_lines = 0; // 1 = normal, 2 = nonempty
char nonprinting = 0; // 1 = normal, 2 = with tabs, 4 = with eol

static size_t cat_write(FILE * stream, unsigned char * buffer, size_t size) {
    if (!nonprinting)
        return fwrite(buffer, 1, size, stream);

    for (size_t i = 0; i < size; i++) {
        if (buffer[i] == '\n') {
            if (nonprinting & 0x4 && fputc('$', stream) == EOF)
                return i;
            if (fputc('\n', stream) == EOF)
                return i;
            continue;
        }
        again:
        if (buffer[i] < ' ' && !(buffer[i] == '\t' && !(nonprinting & 0x2))) {
            if (fputc('^', stream) == EOF ||
                fputc('@' + buffer[i], stream) == EOF)
                return i;
        } else if (buffer[i] == 0x7F) {
            if (fputs("^?", stream) == EOF)
                return i;
        } else if (buffer[i] >= 0x80) {
            buffer[i] -= 0x80;
            goto again;
        } else {
            if (fputc(buffer[i], stream) == EOF)
                return i;
        }
    }
    return size;
}

static char print_fd(int fd, const char * file_name) {
    static size_t line_number = 1;
    if (fd < 0) {
        fprintf(stderr, "cat: cannot access %s: %s\n", file_name, strerror(errno));
        return 0;
    }

    unsigned char buf[CAT_BUFFER];

    ssize_t read_bytes = 0;
    while ((read_bytes = read(fd, buf, CAT_BUFFER)) > 0) { // 0 being EOF
        if (number_lines) {
            for (size_t i = 0; i < read_bytes; i++) {
                char was_blank = 1;
                size_t j = i;
                for (; j < read_bytes; j++) {
                    if (!isspace((char)buf[j]))
                        was_blank = 0;
                    if (buf[j] == '\n' || j == read_bytes - 1) {
                        if (!was_blank || number_lines == 1) {
                            printf("%6ld  ", line_number++);
                            size_t written = 0;
                            if ((written = cat_write(stdout, buf + i, j - i + 1)) == 0) {
                                if (ferror(stdout)) {
                                    perror("cat: standard output");
                                    return 1;
                                }
                                return 0;
                            }
                            if (written != j - i + 1)
                                return 0;
                        } else {
                            if (buf[j] == '\n')
                                printf("\n");
                        }
                        break;
                    }
                }
                i = j;
            }
        } else {
            size_t written = 0;
            if ((written = cat_write(stdout, buf, read_bytes)) == 0) {
                if (ferror(stdout)) {
                    perror("cat: standard output");
                    return 1;
                }
                return 0;
            }
            if (written != read_bytes)
                return 0;
        }
    }

    if (read_bytes < 0) {
        if (file_name)
            fprintf(stderr, "cat: error while reading %s: %s\n", file_name, strerror(errno));
        else
            perror("cat: standard input");
        return 1;
    }
    return 0;
}

static void print_usage() {
    fprintf(stderr,
"Usage: cat [-nbvteAhu] [FILE] ...\n"
"\nConcatenate files to stdout, by default read stdin\n"
"\t-n\tNumber lines\n"
"\t-b\tNumber non-blank lines\n"
"\t-v\tDisplay special characters using the ^ and M- notation\n"
"\t-t\tIncluding tabs\n"
"\t-e\tIncluding \\n as $\n"
"\t-A\tCombine -vte\n"
"\t-h\tThis help message\n"
"\t-u\tPOSIX required (noop)\n");
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        //printf("usage: %s [FILE]...\n", argv[0]);
        return print_fd(STDIN_FILENO, NULL);
    }

    int c = 0;
    while ((c = getopt(argc, argv, "nbvteAhu")) != -1) {
        switch (c) {
            case 'n':
                number_lines = 1;
                break;
            case 'b':
                number_lines = 2;
                break;
            case 'v':
                nonprinting = 1;
                break;
            case 't':
                nonprinting |= 2;
                break;
            case 'e':
                nonprinting |= 4;
                break;
            case 'A':
                nonprinting = 7;
                break;
            case 'h':
            default:
                print_usage();
                return 0;
        }
    }

    int ret = 0;
    char saw_option_end = 0;
    for (int i = 1; i < argc; i++) {
        int fd;
        if (strcmp(argv[i], "-") == 0)
            fd = STDIN_FILENO;
        else if (strcmp(argv[i], "--") == 0 || (argv[i][0] == '-' && !saw_option_end)) {
            saw_option_end = 1;
            continue;
        } else
            fd = open(argv[i], O_RDONLY, 0);
        if (print_fd(fd, argv[i]))
            ret = 1;
    }

    return ret;
}
