#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>

void print_help() {
    fprintf(stderr,
"Usage: realpath [-eEh] file...\n"
"Resolves a file to its full canonical path\n"
"Options:\n"
"\t-e\tFail on nonexistent paths\n"
"\t-E\tTry to at least partially resolve the path\n"
"\t-h\tThis help message\n\n"
"Default is -E\n");
}

static void pop_component(char *path, size_t *len) {
    while (*len > 1 && path[*len - 1] == '/')
        (*len)--;
    while (*len > 1 && path[*len - 1] != '/')
        (*len)--;
    if (*len > 1)
        (*len)--;
}

static char *partial_realpath(const char *path) {
    size_t len = strlen(path);
    if (!len) {
        errno = ENOENT;
        return NULL;
    }
    if (len >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    char *candidate = malloc(len + 1);
    if (!candidate)
        return NULL;
    memcpy(candidate, path, len + 1);

    char *resolved = NULL;
    size_t cut = len;
    while (cut) {
        while (cut > 1 && path[cut - 1] == '/')
            cut--;
        candidate[cut] = 0;
        resolved = realpath(candidate, NULL);
        candidate[cut] = path[cut];
        if (resolved)
            break;
        if (errno != ENOENT) {
            free(candidate);
            return NULL;
        }

        while (cut > 1 && path[cut - 1] == '/')
            cut--;
        while (cut > 1 && path[cut - 1] != '/')
            cut--;
        if (cut == 1 && path[0] != '/')
            cut = 0;
    }

    if (!resolved) {
        resolved = realpath(".", NULL);
        if (!resolved) {
            free(candidate);
            return NULL;
        }
        cut = 0;
    }
    free(candidate);

    size_t out_len = strlen(resolved);
    size_t out_cap = out_len + (len - cut) + 2;
    char *out = malloc(out_cap);
    if (!out) {
        free(resolved);
        return NULL;
    }
    memcpy(out, resolved, out_len + 1);
    free(resolved);

    const char *p = path + cut;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;

        const char *comp = p;
        while (*p && *p != '/')
            p++;
        size_t comp_len = p - comp;

        if (comp_len == 1 && comp[0] == '.')
            continue;
        if (comp_len == 2 && comp[0] == '.' && comp[1] == '.') {
            pop_component(out, &out_len);
            continue;
        }

        if (out[out_len - 1] != '/')
            out[out_len++] = '/';
        memcpy(out + out_len, comp, comp_len);
        out_len += comp_len;
    }
    out[out_len] = 0;
    return out;
}

int print_realpath(const char *path, char partial) {
    char *rpath = realpath(path, NULL);
    if (rpath) {
        puts(rpath);
        free(rpath);
        return 0;
    }

    if (partial && errno == ENOENT) {
        rpath = partial_realpath(path);
        if (rpath) {
            puts(rpath);
            free(rpath);
            return 0;
        }
    }

    fprintf(stderr, "realpath: %s: %s\n", path, strerror(errno));
    return 1;
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-eEh] file...\n%s -h for more information\n", argv[0], argv[0]);
        return 1;
    }
    char partial = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--"))
            break;
        if (argv[i][0] == '-') {
            if (!argv[i][1]) {
                fprintf(stderr, "%s: Invalid option '-'\n", argv[0]);
                return 1;
            }
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'e':
                        partial = 0;
                        break;
                    case 'E':
                        partial = 1;
                        break;
                    case 'h':
                        print_help();
                        return 0;
                    default:
                        fprintf(stderr, "%s: Invalid option '-%c'\n", argv[0], argv[i][j]);
                        return 1;
                }
            }
        }
    }

    char saw_opt_end = 0;
    char errored = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) {
            saw_opt_end = 1;
            continue;
        }
        if (argv[i][0] == '-' && !saw_opt_end)
            continue;
        if (print_realpath(argv[i], partial))
            errored = 1;
    }
    return errored;
}
