#include "../../../libc/src/include/time.h"
#include "../../../libc/src/include/stdio.h"
#include "../../../libc/src/include/stdlib.h"
#include "../../../libc/src/include/unistd.h"


int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("sleep: missing operand\n");
        return EXIT_FAILURE;
    }
    int interval = 0;
    if (sscanf(argv[1], "%d", &interval) != 1) {
        printf("sleep: invalid time interval `%s`\n", argv[1]);
    }

    return sleep(interval);
}