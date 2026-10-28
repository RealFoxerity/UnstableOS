#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>


int main() {
    const char * tty = ttyname(STDIN_FILENO);
    if (!tty) {
        printf("tty: ttyname(): %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", tty);
    return 0;
}