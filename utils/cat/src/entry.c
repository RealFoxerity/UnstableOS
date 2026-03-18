#include "../../../libc/src/include/fcntl.h"
#include "../../../libc/src/include/unistd.h"
#include "../../../libc/src/include/stdlib.h"
#include "../../../libc/src/include/stdio.h"

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("usage: %s [FILE]...\n", argv[0]);
        return 0;
    }

    int fd = open(argv[1], O_RDONLY, 0);
    if (fd < 0) {
        printf("cat: cannot access %s, errno %d\n", argv[1], fd);
        return 1;
    }

    char buf[128];

    ssize_t read_bytes = 0;
    while ((read_bytes = read(fd, buf, 128)) == 128) {
        write(STDOUT_FILENO, buf, 128);
    }

    if (read_bytes < 0) {
        printf("cat: error while reading %s: errno %ld\n", argv[1], read_bytes);
        return 1;
    }

    write(STDOUT_FILENO, buf, read_bytes);
    printf("\n");
    return 0;
}