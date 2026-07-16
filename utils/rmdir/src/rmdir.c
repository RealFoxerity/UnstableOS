#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
int main(int argc, char ** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [DIRECTORY]...\n", argv[0]);
        return 1;
    }
    char success = 1;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            success = 0;
            fprintf(stderr, "%s: cannot remove '%s': %s\n", argv[0], argv[i], strerror(errno));
        }
    }
    return !success;
}