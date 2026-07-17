#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "string.h"
#define CAT_BUFFER 4096
int main(int argc, char ** argv) {
    int fd = -1;
    if (argc < 2) {
        //printf("usage: %s [FILE]...\n", argv[0]);
        fd = STDIN_FILENO;
    } else {
        fd = open(argv[1], O_RDONLY, 0);
    }

    if (fd < 0) {
        fprintf(stderr, "cat: cannot access %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    char buf[CAT_BUFFER];

    ssize_t read_bytes = 0;
    while ((read_bytes = read(fd, buf, CAT_BUFFER)) > 0) { // 0 being EOF
         switch (write(STDOUT_FILENO, buf, read_bytes)) {
             case -1:
                perror("cat: standard output");
                return 1;
             case 0:
                 return 0;
             default: break;
        }
    }

    if (read_bytes < 0) {
        if (argc >= 2)
            fprintf(stderr, "cat: error while reading %s: %s\n", argv[1], strerror(errno));
        else
            perror("cat: standard input");
        return 1;
    }

    return 0;
}
