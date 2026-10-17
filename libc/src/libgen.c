#include <string.h>
#include <libgen.h>

char *basename(char *path) {
    if (!path || !*path)
        return ".";
    if (strcmp(path, "//") == 0)
        return path;
    char * end = path + strlen(path) - 1;
    while (*end == '/' && end > path) *end-- = '\0';
    if (end == path && *path == '/')
        return path;
    char * slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

char *dirname(char *path) {
    if (!path || !*path)
        return ".";
    char * base = basename(path);
    if (base == path) {
        if (*base == '/')
            return base;
        return ".";
    }
    *base = '\0';
    char * end = path + strlen(path) - 1;
    while (*end == '/' && end > path) *end-- = '\0';
    return path;
}