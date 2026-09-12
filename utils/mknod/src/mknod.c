#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <UnstableOS/devs.h>

int main(int argc, char ** argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s NAME TYPE MAJOR MINOR\n", argv[0]);
        fprintf(stderr, "\tTYPE can either be 'b' for block, or 'c' for char\n");
        fprintf(stderr, "\tRefer to /usr/include/UnstableOS/devs.h for numberings\n");
        return 1;
    }

    unsigned mode = 0666;
    if (strcmp(argv[2], "b") == 0) {
        mode |= S_IFBLK;
    } else if (strcmp(argv[2], "c") == 0) {
        mode |= S_IFCHR;
    } else {
        fprintf(stderr, "Unknown type %s\n", argv[2]);
        return 1;
    }

    unsigned short major = 0, minor = 0;
    char * end = NULL;
    major = strtol(argv[3], &end, 0);
    if (end == NULL || *end != '\0') {
        fprintf(stderr, "Invalid major number %s\n", argv[3]);
        return 1;
    }
    minor = strtol(argv[4], &end, 0);
    if (end == NULL || *end != '\0') {
        fprintf(stderr, "Invalid minor number %s\n", argv[4]);
        return 1;
    }

    if (mknod(argv[1], mode, GET_DEV(major, minor)) < 0) {
        perror("mknod");
        return 1;
    }
    return 0;
}