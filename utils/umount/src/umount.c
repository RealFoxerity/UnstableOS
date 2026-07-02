#include <UnstableOS/mount.h>
#include <stdio.h>
int main(int argc, char ** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [mountpoint]\n", argv[0]);
        return 1;
    }

    int ret = umount(argv[1]);
    if (ret < 0)
        perror("umount");
    return ret;
}