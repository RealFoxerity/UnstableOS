#include "include/string.h"
#include "include/unistd.h"
#include "include/stdlib.h"
#include "../../src/include/kernel.h"
#include "include/stdio.h"
#include "include/sys/stat.h"

extern char ** environ;
int exec(const char * path) {
    return syscall(SYSCALL_EXEC, path, (const char *[]){path, NULL}, environ);
}
int execv(const char * path, char * const* argv) {
    return syscall(SYSCALL_EXEC, path, argv, environ);
}
int execve(const char * path, char * const* argv, char * const* envp) {
    return syscall(SYSCALL_EXEC, path, argv, envp);
}

static char * find_file(const char * file) {
    if (file == NULL) return NULL;
    if (file[0] == '/') return strdup(file);

    char * default_paths = "/bin:/sbin";
    char * paths = getenv("PATH");
    char * search_paths = paths;
    if (paths == NULL) search_paths = default_paths;

    search_paths = strdup(search_paths);
    assert(search_paths);

    char * checked_path = strtok(search_paths, ":");
    char * final_path = NULL;

    struct stat info = {0};

    while (checked_path != NULL) {
        final_path = malloc(strlen(checked_path) + 1 + strlen(file) + 1);
        assert(final_path);

        strcpy(final_path, checked_path);
        final_path[strlen(checked_path)] = '/';
        strcpy(final_path + strlen(checked_path) + 1, file);

        if (stat(final_path, &info) == 0)
            return final_path;
        checked_path = strtok(NULL, ":");
        free(final_path);
    }
    free(search_paths);
    return NULL;
}

int execvp(const char * file, char * const* argv) {
    char * final_path = find_file(file);
    if (final_path == NULL) return -1;

    int ret = execv(final_path, argv);
    free(final_path);
    return ret;
}

int execvpe(const char * file, char * const* argv, char * const* envp) {
    char * final_path = find_file(file);
    if (final_path == NULL) return -1;

    int ret = execve(final_path, argv, envp);
    free(final_path);
    return ret;
}