#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <stdlib.h>


static const char * valid_options = "asnrvmh";
static const char * help_message =
"Usage: uname [-asnrvmh]\n"
"Prints information about this system\n\n"
"    -a    Prints all information in this order\n"
"    -s       ... the name of the running kernel\n"
"    -n       ... the current hostname\n"
"    -r       ... the running kernel release\n"
"    -v       ... the running kernel version\n"
"    -m       ... the machine type\n"
"    -h       ... this help message\n\n"
"If no options are specified, acts as -s\n";

__attribute__((noreturn)) void help() {
    printf(help_message);
    exit(EXIT_SUCCESS);
}
int main(int argc, char ** argv) {
    int c;
    int spec = 0;
    while ((c = getopt(argc, argv, valid_options)) != -1) {
        switch (c) {
            case 'h':
                help();
            case 'a':
                spec = 31;
                break;
            case 's':
                spec |= 1;
                break;
            case 'n':
                spec |= 2;
                break;
            case 'r':
                spec |= 4;
                break;
            case 'v':
                spec |= 8;
                break;
            case 'm':
                spec |= 16;
                break;
            default:
                usage:
                fprintf(stderr, "Try '%s -h' for more information.\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (argv[optind])
        goto usage;

    if (!spec)
        spec = 1;

    struct utsname name;
    if (uname(&name) < 0) {
        fprintf(stderr, "uname: uname(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (spec & 1)
        printf("%s", name.sysname);
    if (spec & 2)
        printf("%s%s", spec & 1 ? " " : "", name.nodename);
    if (spec & 4)
        printf("%s%s", spec & 3 ? " " : "", name.release);
    if (spec & 8)
        printf("%s%s", spec & 7 ? " " : "", name.version);
    if (spec & 16)
        printf("%s%s", spec & 15 ? " " : "", name.machine);
    printf("\n");
    return 0;
}