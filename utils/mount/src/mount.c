#include <UnstableOS/mount.h>
#include <string.h>
#include <stdio.h>
int main(int argc, char ** argv) {
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "Usage: %s [fs_type] [srcdev] [mountpoint] <options>\n", argv[0]);
        fprintf(stderr, "Supported filesystems:\n\tdevfs\n\ttarfs\n");
        fprintf(stderr, "Supported options:\n\tro\n\trw (default)\n");
        return 1;
    }
    int fs_type = 0;
    int options = 0;

    if (strcmp(argv[1], "devfs") == 0)
        fs_type = FS_DEVFS;
    else if (strcmp(argv[1], "tarfs") == 0)
        fs_type = FS_TARFS;
    else {
        fprintf(stderr, "mount: Unknown file system type %s\n", argv[1]);
        return 1;
    }

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

    int ret = mount(argv[2], argv[3], fs_type, options);
    if (ret < 0)
        perror("mount");
    return ret;
}