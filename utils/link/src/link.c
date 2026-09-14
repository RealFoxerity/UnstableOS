#include <stdio.h>
#include <unistd.h>
int main(int argc, char ** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s TARGET LINK\n", argv[0]);
        return 1;
    }

    if (link(argv[1], argv[2]) < 0) {
        perror("link");
        return 1;
    }
    return 0;
}