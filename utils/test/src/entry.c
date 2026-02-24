#include "../../../libc/src/include/stdio.h"
#include "../../../libc/src/include/string.h"
#include "../../../libc/src/include/stdlib.h"
#include "../../../src/include/kernel.h"
#include "../../../libc/src/include/uthreads.h"
#include "../../../libc/src/include/ctype.h"
#include <stdint.h>

extern void malloc_print_heap_objects();

int test_thread(struct uthread_args * self, void* test_val) {
    printf("Peak testing thread :3 random magic value: %ld\n", (long)test_val);
    for (int i = 0; i < 10000000; i++) {
        for (int i = 0; i < 100; i++);
    }
    return 0;
}

void show_help() {
    printf("\nr - print a random number\nmalloc [num] - run malloc with size as integer\n"
                "heap - print heap\nt - run a test thread, get return code\nexit - exits\n"
                "open [path] - opens a file\nread [fd] [amount] - reads from file\n"
                "seek [+/-/ ] [fd] [off] - seeks into a file - ahead, from end, set\n"
                "ls [path] - lists directory\nexec [path] - runs an ELF file\n");
}


void print_hex_buf(const unsigned char * buf, size_t n) {
    if (n > 16)
        for (size_t i = 0; i < n - 16; i += 16) {
            if (n > UINT16_MAX) printf("%lx  ", i);
            else printf("%hx  ", (unsigned short)i);

            for (int j = 0; j < 16; j++) {
                printf("%hhx ", buf[i + j]);
            }
            for (int j = 0; j < 16; j++) {
                printf("%c", isprint(buf[i + j]) ? buf[i + j] : '.');
            }
            printf("\n");
        }
    if (n % 16 != 0) {
        if (n > UINT16_MAX) printf("%lx  ", n - (n%16));
        else printf("%hx  ", (unsigned short)(n - (n%16)));
        for (int i = 0; i < n%16; i++) {
            printf("%hhx ", buf[n - (n%16) + i]);
        }
        for (int i = 0; i < 16 - (n%16); i++) {
            printf("   ");
        }
        for (int i = 0; i < 16 - (n%16); i++) {
            printf("%c", isprint(buf[n - (n%16) + i]) ? buf[n - (n%16) + i] : '.');
        }
        printf("\n");
    }
}

#define MAX_INPUT_BUFFER 128
int main() {
    printf("Testing shell env, H for help\n");
    char input_buf[MAX_INPUT_BUFFER];
    ssize_t read_bytes = 0;
    while(1) {
        memset(input_buf, 0, MAX_INPUT_BUFFER);
        printf("> ");
        read_bytes = read(0, input_buf, MAX_INPUT_BUFFER - 1);
        assert(read_bytes > 0);

        // strcmp here ok since we don't read into the last null byte
        if (strcmp("H\n", input_buf) == 0) {
            show_help();
        } else if (strcmp("r\n", input_buf) == 0) {
            printf("%d\n", rand());
        } else if (strcmp("t\n", input_buf) == 0) {
            uthread_t thread = uthread_create(test_thread, (void*)(long)rand());
            printf("%d\n", uthread_join(thread));
        } else if (strncmp("malloc ", input_buf, 7) == 0) {
            char * end = NULL;
            unsigned long amount = 0;

            if(sscanf(input_buf, "malloc %lu", &amount) != 1) {
                printf("Bad argument!\n");
                continue;
            }

            printf("Allocated %lu bytes at address 0x%p\n", amount, malloc(amount));
        } else if (strcmp("heap\n", input_buf) == 0) {
            malloc_print_heap_objects();
        } else if (strcmp("exit\n", input_buf) == 0) {
            exit(EXIT_SUCCESS);
        } else if (strcmp("open ", input_buf) == 0) {
            input_buf[read_bytes - 1] = '\0'; // get rid of new line
            char * path = input_buf + 5;
            printf("New fd: %d\n", open(path, O_RDWR, 0));
        } else if (strcmp("read ", input_buf) == 0) {
            unsigned long amount = 0;
            int fd = -1;
            if (sscanf(input_buf, "read %d %lu", &fd, &amount) != 2) {
                printf("Bad argument!\n");
                continue;
            }
            unsigned char * buf = malloc(amount);
            assert(buf);
            ssize_t read_bytes = read(fd, buf, amount);
            if (read_bytes < 0) {
                printf("Error while reading: %ld\n", read_bytes);
                continue;
            }

            printf("Read %lu bytes:\n", read_bytes);
            print_hex_buf(buf, read_bytes);
        } else if (strcmp("seek + ", input_buf) == 0) {
            off_t off = 0;
            int fd = -1;
            if (sscanf(input_buf, "seek + %d %ld", &fd, &off) != 2) {
                printf("Bad argument!\n");
                continue;
            }

            printf("Seek: %ld\n", seek(fd, off, SEEK_CUR));
        } else if (strcmp("seek - ", input_buf) == 0) {
            off_t off = 0;
            int fd = -1;
            if (sscanf(input_buf, "seek - %d %ld", &fd, &off) != 2) {
                printf("Bad argument!\n");
                continue;
            }

            printf("Seek: %ld\n", seek(fd, off, SEEK_END));
        } else if (strcmp("seek ", input_buf) == 0) {
            off_t off = 0;
            int fd = -1;
            if (sscanf(input_buf, "seek %d %ld", &fd, &off) != 2) {
                printf("Bad argument!\n");
                continue;
            }

            printf("Seek: %ld\n", seek(fd, off, SEEK_SET));
        } else if (strcmp("ls ", input_buf) == 0) {

        } else if (strcmp("exec ", input_buf) == 0) {
            input_buf[read_bytes - 1] = '\0'; // get rid of new line
            char * path = input_buf + 5;
            printf("Uh-oh exec() failed with %d\n", exec(path));
        } else printf("?");
    }
}