#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

int main(int argc, char ** argv) {
    if (argc < 2) {
        fprintf(stderr, "%1$s: missing operand\nTry `%1$s -h` for more information.\n", argv[0]);
        return 1;
    }

    char nl = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            printf(
"Usage: %s [-n] FILE...\n"
"Reads out the target of a given symbolic link\n\n"
"\t-n\tDo not output the trailing newline\n", argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-n") == 0)
            nl = 0;
        else if (strcmp(argv[i], "--") == 0)
            break;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "%1$s: invalid option `%2$s`\nTry `%1$s -h` for more information.\n", argv[0], argv[i]);
            return 1;
        }
    }

    char pathbuf[SYMLINK_MAX+1];
    char saw_end = 0;
    char failed = 0;
    char printed = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            saw_end = 1;
            continue;
        }
        if (!saw_end && argv[i][0] == '-')
            continue;
        ssize_t read = readlink(argv[i], pathbuf, SYMLINK_MAX);
        if (read < 0) {
            if (printed)
                printf("\n");
            fprintf(stderr, "%s: readlink(%s): %s", argv[0], argv[i], strerror(errno));
            printed = 1;
            failed = 1;
            continue;
        }
        pathbuf[read] = '\0';
        if (printed)
            printf("\n");
        printf("%s", pathbuf);
        printed = 1;
    }
    if (nl)
        printf("\n");
    return failed;
}