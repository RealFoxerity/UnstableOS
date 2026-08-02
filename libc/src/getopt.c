#include <string.h>
#include <stdio.h>
#include <ctype.h>

char * optarg = NULL;
int opterr = 1, optind = 1, optopt = '\0';

int getopt(int argc, char * const argv[], const char *optstring) {
    if (!optstring || !argv || argc <= 0)
        return -1;

    static int argument_pos = 0;

    if (optind == 0)
        optind = 1;
    if (argv[optind][argument_pos] == '\0') {
        argument_pos = 0;
        if (++optind >= argc)
            return -1;
    }
    if (optind >= argc || !argv[optind] ||
        *argv[optind] != '-' || strcmp(argv[optind], "-") == 0)
            return -1;
    if (strcmp(argv[optind], "--") == 0)
        return argument_pos = 0, optind++, -1;

    if (argument_pos == 0)
        argument_pos = 1; // skip the -

    char report_missing_args = 0;
    size_t start = 0;
    // skip all leading + (posix requires only 1)
    for (size_t i = 0; optstring[i] != '\0'; i++) {
        if (optstring[i] == '+') continue;
        start = i;
        if (optstring[i] == ':') {
            report_missing_args = 1;
            start++;
        }
        break;
    }

    size_t i = start;

    if (argv[optind][argument_pos] == ':') goto error;

    for (; optstring[i] != '\0'; i++) {
        if (optstring[i] == '+' || !isalnum(optstring[i]))
            continue;
        if (optstring[i] == argv[optind][argument_pos])
            break;
    }

    error:
    // not sure if optopt can be used for normal exit?
    optopt = (int)argv[optind][argument_pos];
    argument_pos++;

    if (optstring[i] == '\0' || optopt == ':') {
        if (opterr)
            fprintf(stderr, "%s: unknown option: '%c'\n", argv[0], optopt);
        return '?';
    }

    if (optstring[i + 1] == ':') {
        if (argv[optind][argument_pos] == '\0') {
            argument_pos = 0;
            optind ++;
            if (optind >= argc) {
                if (report_missing_args)
                    return ':';
                if (opterr)
                    fprintf(stderr, "%s: missing argument for option: '%c'\n", argv[0], optopt);
                return '?';
            }
        }
        optarg = argv[optind++];
        optarg += argument_pos;
        argument_pos = 0;
    }
    return optopt;
}