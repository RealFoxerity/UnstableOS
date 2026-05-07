#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <stddef.h>

int main(int argc, char *argv[]) {
    char path[PATH_MAX] = {0};
    if (getcwd(path, PATH_MAX) == NULL) {
        perror("pwd: getcwd");
        return -1;
    }
    printf("%s\n", path);
    return 0;
}