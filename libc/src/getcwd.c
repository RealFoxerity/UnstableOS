#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <string.h>

char *getcwd(char *buf, size_t size) {
    if (size < 2) return NULL;
    if (buf == NULL) return NULL;

    struct stat statbuf;

    buf[size - 1] = '\0';
    size_t remaining = size - 1; // -1 for null byte

    dev_t curr_dir_dev = -1;
    ino_t curr_dir_ino = -1;

    int curr_dir_fd = -1, parent_dir_fd = -1;

    curr_dir_fd = open(".", O_SEARCH | O_DIRECTORY);
    if (curr_dir_fd == -1) return NULL;


    DIR * this_dir = NULL;

    while (1) {
        // fstat shouldn't be undefined behavior
        // posix says that after fdopendir, only modifying and closing is undefined
        if (fstat(curr_dir_fd, &statbuf) == -1) {
            close(curr_dir_fd);
            return NULL;
        }

        curr_dir_ino = statbuf.st_ino;
        curr_dir_dev = statbuf.st_dev;

        parent_dir_fd = openat(curr_dir_fd, "..", O_SEARCH | O_DIRECTORY);
        if (this_dir == NULL)
            close(curr_dir_fd);
        else
            closedir(this_dir); // also closes curr_dir_fd because of how fdopendir work

        if (parent_dir_fd == -1) return NULL;

        curr_dir_fd = parent_dir_fd;

        this_dir = fdopendir(curr_dir_fd);
        // failed to open parent directory
        if (this_dir == NULL) {
            close(curr_dir_fd);
            return NULL;
        }

        struct dirent * dent = readdir(this_dir);
        // failed to read contents
        if (dent == NULL) {
            closedir(this_dir);
            return NULL;
        }


        while (1) {
            dent = readdir(this_dir);

            // somehow didn't find the target inode
            if (dent == NULL) {
                closedir(this_dir);
                return NULL;
            }

            if (fstatat(curr_dir_fd, dent->d_name, &statbuf, 0) == -1) {
                closedir(this_dir);
                return NULL;
            }
            if (statbuf.st_ino == curr_dir_ino && statbuf.st_dev == curr_dir_dev) break;
        }

        size_t component_len = strlen(dent->d_name);
        if (component_len == 2 && strcmp(dent->d_name, "..") == 0) break;

        if (remaining < component_len) {
            closedir(this_dir);
            return NULL;
        }

        memcpy(buf + remaining - component_len, dent->d_name, component_len);
        remaining -= component_len;
        buf[--remaining] = '/';
    }
    closedir(this_dir);

    if (remaining == size - 1) {
        // we are at root, so most of the logic is skipped
        buf[0] = '/';
        buf[1] = '\0';
    } else {
        // move stuff back to the buffer beginning
        memmove(buf, buf + remaining, size - remaining);
    }
}
