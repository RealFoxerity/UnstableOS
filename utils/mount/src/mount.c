#include <UnstableOS/mount.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int main(int argc, char ** argv) {
    if (argc == 1) {
        FILE * mounts = fopen("/proc/mounts", "r");
        if (!mounts) {
            fprintf(stderr, "mount: /proc/mounts: %s\n", strerror(errno));
            return 1;
        }

        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, 4096, mounts)) > 0)
            fwrite(buf, 1, n, stdout);

        return 0;
    }

    if (argc < 4 || argc > 5) {
        fprintf(stderr, "Usage: %s [fs_type] [srcdev] [mountpoint] <options>\n", argv[0]);
        fprintf(stderr, "Supported filesystems:\n\ttmpfs\n\tdevfs\n\tprocfs\n\ttarfs\n\tfat\n\text2\n");
        fprintf(stderr, "Supported options:\n\tro\n\trw (default)\n");
        return 1;
    }
    int options = 0;

    if (argc == 5) {
        if (strcmp(argv[4], "rw") == 0)
            options = 0;
        if (strcmp(argv[4], "ro") == 0)
            options = MOUNT_RDONLY;
        else {
            fprintf(stderr, "mount: Unknown option %s\n", argv[4]);
            return 1;
        }
    }

    if (mount(argv[2], argv[3], argv[1], options) < 0) {
        if (errno == ENODEV) {
            fprintf(stderr, "mount: Unsupported filesystem type\n");
            return 1;
        }
        perror("mount");
        return 1;
    }
    return 0;
}