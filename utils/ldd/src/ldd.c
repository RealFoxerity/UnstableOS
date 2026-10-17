#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

extern char **environ;

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s EXEC\nLists dynamic dependencies\n", argv[0]);
    }
    setenv("LD_TRACE_LOADED_OBJECTS", "1", 0);
    execve("/usr/lib/rtld.so", argv, environ);
    fprintf(stderr, "%s: execve: %s\n", argv[0], strerror(errno));
    return 1;
}