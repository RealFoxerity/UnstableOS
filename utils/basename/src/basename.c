#include <stdio.h>
#include <libgen.h>
#include <string.h>

int main(int argc, char ** argv) {
    if (argc < 2) {
        fprintf(stderr, "%s: missing operand\n", argv[0]);
        return 1;
    }
    char parsed = 0;
    char saw_arg_end = 0;
    for (int i = 1; i < argc; i++) {
        if (!saw_arg_end && argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0) {
                saw_arg_end = 1;
                continue;
            }
            fprintf(stderr, "%s: invalid option %s\n", argv[0], argv[i]);
            return 1;
        }
        parsed = 1;
    }
    if (!parsed) {
        fprintf(stderr, "%s: missing operand\n", argv[0]);
        return 1;
    }
    saw_arg_end = 0;
    for (int i = 1; i < argc; i++) {
        if (!saw_arg_end && argv[i][0] == '-') {
            saw_arg_end = 1;
            continue;
        }
        printf("%s\n", basename(argv[i]));
    }
    return 0;
}