#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
int main(int argc, char ** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [FILE]...\n", argv[0]);
        return 1;
    }
    char success = 1;
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            success = 0;
            fprintf(stderr, "%s: cannot remove '%s': %s\n", argv[0], argv[i], strerror(errno));
        }
    }
    return !success;
}