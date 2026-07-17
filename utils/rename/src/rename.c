#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
int main(int argc, char ** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s [old] [new]\n", argv[0]);
        return 1;
    }
    if (rename(argv[1], argv[2]) < 0) {
        fprintf(stderr, "%s: cannot rename '%s': %s\n", argv[0], argv[1], strerror(errno));
        return 1;
    }
    return 0;
}