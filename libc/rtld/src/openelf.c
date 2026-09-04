#include "basic.h"
#include "string.h"
#include "files.h"
#include <limits.h>

static int _openelf(const char * name, const char * runpath) {
    unsigned long path_len = strlen(name);
    static char path_buf[PATH_MAX];
    const char * libdir = runpath;

    while (libdir && *libdir) {
        if (libdir[0] == '\0')
            break;
        const char * next_colon = strchrnul(libdir, ':');
        if (!*next_colon)
            break;
        next_colon ++;

        if (next_colon == libdir + 1)
            goto next;
        if (next_colon - libdir + path_len >= PATH_MAX)
            goto next;

        strncpy(path_buf, libdir, next_colon - libdir);
        path_buf[next_colon - libdir - 1] = '/';
        strcpy(path_buf + (next_colon - libdir), name);

        int fd = openat(AT_FDCWD, path_buf, O_RDONLY);
        if (fd >= 0) {
            //dbg_print(path_buf);
            return fd;
        }

        next:
        libdir = next_colon;
    }
    return -1;
}

int openelf(const char * name, const char * runpath) {
    if (name[0] == '/') {
        int fd = openat(AT_FDCWD, name, O_RDONLY);
        if (fd >= 0) {
            //dbg_print(name);
            return fd;
        }
        goto fail;
    }

    int fd = _openelf(name,ld_path);
    if (fd < 0 && runpath) {
        fd = _openelf(name, runpath);
    }
    if (fd >= 0)
        return fd;

    fail:
    // failed\n to complete the "Loading dependency `x`..."
    dbg_print("failed\nrtld: Can't find shared object, giving up\n");
    abort();
}