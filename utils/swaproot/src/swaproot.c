#include <stdio.h>
#include <unistd.h>
#include <UnstableOS/syscalls.h>
#include <string.h>

int main(int argc, char ** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s NEW_ROOT OLD_ROOT_MNT\n", argv[0]);
        return 1;
    }

    int ret = syscall(SYSCALL_SWAPROOT, argv[1], argv[2]);
    if (ret < 0) {
        fprintf(stderr, "%s: swaproot: %s\n", argv[0], strerror(-ret));
        return 1;
    }
    return 0;
}
