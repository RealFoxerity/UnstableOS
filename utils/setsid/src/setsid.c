#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("setsid: no command specified\n");
        return EXIT_FAILURE;
    }
    switch (fork()) {
        case -1:
            perror ("setsid: fork");
            return EXIT_FAILURE;
        case 0:
            if (setsid() < 0) {
                perror ("setsid: setsid");
                return EXIT_FAILURE;
            }
            execvp(argv[1], &argv[1]);
            perror ("setsid: execvp");
            return EXIT_FAILURE;
        default:
            return EXIT_SUCCESS;
    }
}