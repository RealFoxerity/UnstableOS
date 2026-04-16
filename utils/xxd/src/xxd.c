#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <errno.h>

#define XXD_READ_BUFFER 256


void print_buffer(const unsigned char * buf, size_t size) {
    static size_t bytes_printed = 0;
    printf("%0lx: ", bytes_printed);
    for (int i = 0; i < 16; i++) {
        switch (buf[i]) {
            case '\t':
            case '\n':
            case '\r':
                printf("\e[33m");
                break;
            case ' ' ... 0x7F:
                printf("\e[32m");
                break;
            case 0:
                printf("\e[0m");
                break;
            case 0xFF:
                printf("\e[36m");
                break;
            default: printf("\e[31m");
        }
        printf("%02hhx\e[0m ", buf[i]);
    }
    printf(" ");
    for (int i = 0; i < 16; i++) {
        switch (buf[i]) {
            case '\t':
            case '\n':
            case '\r':
                printf("\e[33m.");
                break;
            case ' ' ... 0x7E:
                printf("\e[32m%c", buf[i]);
                break;
            case 0:
                printf("\e[0m.");
                break;
            case 0xFF:
                printf("\e[36m.");
                break;
            default: printf("\e[31m.");
        }
        printf("\e[0m");
    }
    printf("\n");
    bytes_printed += size;
}

int main(int argc, char ** argv) {
    int in_fd = STDIN_FILENO;
    if (argc == 2) {
        in_fd = open(argv[1], O_RDONLY, 0);
        if (in_fd < 0) {
            fprintf(STDERR_FILENO, "xxd: %s: %s\n", argv[1], strerror(errno));
            return EXIT_FAILURE;
        }
    }

    unsigned char buf[XXD_READ_BUFFER];
    unsigned char print_buf[16];

    ssize_t read_bytes = 0;
    ssize_t print_buf_size = 0;

    ssize_t parsed_bytes = 0;
    while ((read_bytes = read(in_fd, buf, XXD_READ_BUFFER)) > 0) { // 0 being EOF
        parsed_bytes = read_bytes;
        while (print_buf_size + parsed_bytes > 16) {
            memcpy(print_buf + print_buf_size, buf + read_bytes - parsed_bytes, 16 - print_buf_size);
            parsed_bytes -= 16 - print_buf_size;
            print_buf_size = 0;
            print_buffer(print_buf, 16);
        }

        memcpy(print_buf + print_buf_size, buf + read_bytes - parsed_bytes, parsed_bytes);
        print_buf_size += parsed_bytes;
    }

    print_buffer(print_buf, print_buf_size);

    if (read_bytes < 0) {
        printf("xxd: error while reading %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    return 0;
}
