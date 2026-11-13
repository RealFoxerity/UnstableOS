#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>

// yes, this is chmod.c copied over

// TODO: this implementation doesn't support the insanely goofy POSIX options
// e.g. a+=, go+-w, g=o, 'o=u -g'
// that is, assigning selectors to selectors and combined operations

struct arg {
    mode_t mode;
    unsigned char op; // 0 = '=', 1 = '+', 2 = '-'
    char do_X;
    struct arg *next; // to not do reallocations
};
mode_t umode = 0;
int parse_arg(const char * arg, struct arg * out) {
    mode_t mode = 0;
    mode_t special_modes = 0;

    if (isdigit(arg[0])) {
        char * end;
        unsigned long conv = strtoul(arg, &end, 8);
        if (!end || (*end != ',' && *end != '\0'))
            return 0;
        if (conv > 07777)
            return 0;
        out->mode = conv;
        out->op   = 0;
        out->do_X = 0;
        return 1;
    }

    unsigned char selector = 0; // 1 = o, 2 = g, 4 = u
    unsigned char op = 0;       // 0 = '=', 1 = '+', 2 = '-'
    unsigned char state = 0;    // 0 = waiting on selector, 1 = waiting on op, 2 = waiting on mode

    char do_X = 0;
    char adjust_to_umask = 0;

    for (size_t i = 0; arg[i] && arg[i] != ','; i++) {
        unsigned char sel = 1;
        unsigned char part_mode = 1;
        switch (arg[i]) {
            case 'u':
                sel <<= 1;
            case 'g':
                sel <<= 1;
            case 'o':
                if (state > 1)
                    return 0;
                state = 1;
                selector |= sel;
                break;
            case 'a':
                if (state > 1)
                    return 0;
                state = 1;
                selector = 0b111;
                break;
            case '-':
                op++;
            case '+':
                op++;
            case '=':
                if (state != 1) {
                    if (arg[i] == '=')
                        return 0;
                    adjust_to_umask = 1;
                }
                state = 2;
                break;
            case 'r':
                part_mode <<= 1;
            case 'w':
                part_mode <<= 1;
            case 'x':
                if (state != 2)
                    return 0;
                mode |= part_mode;
                break;
            case 's':
                if (state != 2)
                    return 0;
                if (selector & 4)
                    special_modes |= S_ISUID;
                if (selector & 2)
                    special_modes |= S_ISGID;
                break;
            case 't':
                if (state != 2)
                    return 0;
                special_modes |= S_ISVTX;
                break;
            case 'X':
                if (state != 2)
                    return 0;
                do_X = 1;
                break;
            default:
                return 0;
        }
    }
    if (state != 2 /*|| (mode | special_modes) == 0*/) // believe it or not, but 'a=' means a-rwx
        return 0;
    // stuff like u= so it doesn't nuke all bits
    if ((mode | special_modes) == 0 && op == 0) {
        op = 2;
        mode = 7;
        if (selector & 1)
            special_modes |= S_ISVTX;
        if (selector & 2)
            special_modes |= S_ISGID;
        if (selector & 4)
            special_modes |= S_ISUID;
    }
    mode_t final_mode = 0;
    if (selector & 1)
        final_mode |= mode;
    if (selector & 2)
        final_mode |= mode << 3;
    if (selector & 4)
        final_mode |= mode << 6;
    final_mode |= special_modes;

    if (adjust_to_umask)
        final_mode &= ~umode;

    out->mode = final_mode;
    out->op   = op;
    out->do_X = do_X;
    return 1;
}
int parse_mode(const char * arg, struct arg ** out) {
    struct arg * last = NULL;
    do {
        struct arg * curr = malloc(sizeof(struct arg));
        if (!curr) {
            perror("mkdir: malloc()");
            exit(1);
        }
        if (!parse_arg(arg, curr))
            return 0;

        if (last)
            last->next = curr;
        last = curr;
        if (!*out)
            *out = curr;

        arg = strchr(arg, ',');
        if (arg) arg++;
    } while (arg);
    last->next = NULL;
    return 1;
}

mode_t get_mode(struct arg * modes) {
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    struct arg * m = modes;
    while (m) {
        switch (m->op) {
            case 0: // =
                mode = m->mode;
                break;
            case 1: // +
                mode |= m->mode;
                break;
            case 2: // -
                mode &= ~m->mode;
                break;
            default:
                break;
        }
        m = m->next;
    }
    return mode;
}

void print_help() {
    fprintf(stderr,
"Usage: mkdir [-h] [-m mode,...] [-p] folder...\n"
"Creates folders\n\n"
"Options:\n"
"\t-m\tSet permissions other than a=rwx (see chmod)\n"
"\t-p\tCreate any missing intermediate folders\n"
"\t-h\tDisplay this help message\n");
}

int main(int argc, char ** argv) {
    if (argc < 2 || argc > 5) {
        usage:
        fprintf(stderr, "Usage: %s [-m mode,...] [-p] folder...\n%s -h for more information\n", argv[0], argv[0]);
        return 1;
    }

    umode = umask(0);
    umask(umode); // restore the original one, just for funsies :3

    char saw_arg_end = 0;
    char create_intermediate = 0;
    size_t mode_index = 0;
    struct arg * modes = NULL;
    mode_t mode = 0777;
    // we need to do double pass to get the -m and -p
    for (int i = 1; i < argc; i++) {
        if (strcmp("--", argv[i]) == 0)
            break;
        if (argv[i][0] != '-')
            continue;
        if (!argv[i][1] || argv[i][1 + strspn(argv[i] + 1, "hpm")]) {
            fprintf(stderr, "mkdir: Unknown option %s\n", argv[i]);
            return 1;
        }
        for (int j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
                case 'h':
                    print_help();
                    return 0;
                case 'm':
                    if (mode_index)
                        goto usage;
                    mode_index = ++i;
                    if (i >= argc || argv[i] == NULL)
                        goto usage;
                    if (!parse_mode(argv[i], &modes)) {
                        fprintf(stderr, "mkdir: Invalid mode specifier %s\n", argv[i]);
                        return 1;
                    }
                    mode = get_mode(modes);
                    break;
                case 'p':
                    create_intermediate = 1;
                default:
                    break;
            }
        }
    }

    int errored = 0;
    for (int i = 1; i < argc; i++) {
        if (!saw_arg_end) {
            if (strcmp("--", argv[i]) == 0) {
                saw_arg_end = 1;
                continue;
            }
            if (argv[i][0] == '-')
                continue;
        }
        if (i == mode_index)
            continue;

        if (!create_intermediate && mkdir(argv[i], mode) < 0) {
            fprintf(stderr, "mkdir: '%s': %s\n", argv[i], strerror(errno));
            errored = 1;
        }
        if (create_intermediate) {
            char * p = strdup(argv[i]);
            if (!p) {
                perror("mkdir: strdup()");
                return 1;
            }

            char * start = argv[i] + (argv[i][0] == '/' ? 1 : 0);
            char * tok = strtok(start, "/");
            while (tok) {
                char * tok2 = strtok(NULL, "/");
                if (!tok2) {
                    if (mkdir(p, mode) < 0) {
                        fprintf(stderr, "mkdir: '%s': %s\n", p, strerror(errno));
                        errored = 1;
                    }
                    break;
                }
                p[tok2 - argv[i] - 1] = '\0';
                if (mkdir(p, 0777) < 0 && errno != EEXIST) {
                    p[tok2 - argv[i] - 1] = '/';
                    fprintf(stderr, "mkdir: '%s': %s\n", p, strerror(errno));
                    errored = 1;
                    break;
                }
                p[tok2 - argv[i] - 1] = '/';
                tok = tok2;
            }
            free(p);
        }
    }
    return errored;
}