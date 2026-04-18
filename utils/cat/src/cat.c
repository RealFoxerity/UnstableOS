#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "string.h"

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("usage: %s [FILE]...\n", argv[0]);
        return 0;
    }

    int fd = open(argv[1], O_RDONLY, 0);
    if (fd < 0) {
        printf("cat: cannot access %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    char buf[128];

    ssize_t read_bytes = 0;
    while ((read_bytes = read(fd, buf, 128)) > 0) { // 0 being EOF
        write(STDOUT_FILENO, buf, read_bytes);
    }

    if (read_bytes < 0) {
        printf("cat: error while reading %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    printf("\n");
    return 0;
}
