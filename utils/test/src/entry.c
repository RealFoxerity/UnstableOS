#include "../../../libc/src/include/stdio.h"
#include "../../../libc/src/include/string.h"
#include "../../../libc/src/include/stdlib.h"
#include "../../../src/include/kernel.h"
#include "../../../libc/src/include/uthreads.h"

extern void malloc_print_heap_objects();

int test_thread(struct uthread_args * self, void* _) {
    printf("Peak testing thread :3\n");
    for (int i = 0; i < 10000000; i++) {
        for (int i = 0; i < 100; i++);
    }
    return 0;
}

void show_help() {
    printf("\nr - print a random number\nmalloc [num] - run malloc with size as integer\nheap - print heap\nt - run a test thread, get return code\nexit - exits\n"
                "open [path] - opens a file\nread [fd] [amount] - reads from file\nls [path] - lists directory\n");
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
        assert(read_bytes >= 0);

        // strcmp here ok since we don't read into the last null byte
        if (strcmp("H\n", input_buf) == 0) {
            show_help();
        } else if (strcmp("r\n", input_buf) == 0) {
            printf("%d\n", rand());
        } else if (strcmp("t\n", input_buf) == 0) {
            uthread_t thread = uthread_create(test_thread, NULL);
            printf("%d\n", uthread_join(thread));
        } else if (strncmp("malloc ", input_buf, 7) == 0) {
            char * end = NULL;
            unsigned long amount = strtol(input_buf + 7, &end);
            if (end == input_buf + 7) { // no conversion
                printf("Bad argument\n");
                continue;
            }
            printf("Allocated %d bytes at address 0x%x\n", amount, malloc(amount));
        } else if (strcmp("heap\n", input_buf) == 0) {
            malloc_print_heap_objects();
        } else if (strcmp("exit\n", input_buf) == 0) {
            exit(EXIT_SUCCESS);
        } else if (strcmp("open ", input_buf) == 0) {

        } else if (strcmp("read ", input_buf) == 0) {

        } else if (strcmp("ls ", input_buf) == 0) {

        } else printf("?");
    }
}