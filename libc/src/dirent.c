#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include "../../src/include/kernel.h"
#include "../../src/include/errno.h"

#include <assert.h>
#define MAX_FILENAME_LEN 512

DIR * fdopendir(int fd) {
    if (fd < 0) return NULL;
    DIR * new = malloc(sizeof(DIR) + MAX_FILENAME_LEN);
    assert(new);
    new->dent_size = sizeof(DIR) + MAX_FILENAME_LEN;
    new->fd = fd;
    return new;
}
DIR * opendir(const char * filename) {
    if (filename == NULL) return NULL;
    int fd = open(filename, O_RDONLY | O_DIRECTORY, 0);
    return fdopendir(fd);
}
int dirfd(DIR * dirp) {
    if (dirp == NULL) return EINVAL;
    return dirp->fd;
}
int closedir(DIR * dirp) {
    if (dirp == NULL) {
        errno = EBADF;
        return -1;
    }

    int fd = dirp->fd;
    free(dirp);
    return close(fd);
}

struct dirent * readdir(DIR * dirp) {
    if (dirp == NULL) return NULL;
    ssize_t ret = syscall(SYSCALL_READDIR, dirp->fd, &dirp->dent, dirp->dent_size);
    if (ret <= 0) return NULL;
    return &dirp->dent;
}
void rewinddir(DIR * dirp) {
    if (dirp == NULL) return;
    lseek(dirp->fd, 0, SEEK_SET);
}
void seekdir(DIR * dirp, off_t loc) {
    if (dirp == NULL) return;
    lseek(dirp->fd, loc, SEEK_SET);
}
off_t telldir(DIR * dirp) {
    if (dirp == NULL) {
        errno = EBADF;
        return -1;
    }
    return lseek(dirp->fd, 0, SEEK_CUR);
}
