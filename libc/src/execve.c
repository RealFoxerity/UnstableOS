#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <UnstableOS/syscalls.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>

extern char ** environ;
int exec(const char * path) {
    ___set_errno(-syscall(SYSCALL_EXEC, path, (const char *[]){path, NULL}, environ));
    return -1;
}
int execv(const char * path, char * const* argv) {
    ___set_errno(-syscall(SYSCALL_EXEC, path, argv, environ));
    return -1;
}
int execve(const char * path, char * const* argv, char * const* envp) {
    ___set_errno(-syscall(SYSCALL_EXEC, path, argv, envp));
    return -1;
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

    char * saveptr = NULL;
    char * checked_path = strtok_r(search_paths, ":", &saveptr);
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
        checked_path = strtok_r(NULL, ":", &saveptr);
        free(final_path);
    }
    free(search_paths);
    return NULL;
}

int execvp(const char * file, char * const* argv) {
    char * final_path = find_file(file);
    if (final_path == NULL) return -1;

    ___set_errno(execv(final_path, argv));
    free(final_path);
    return -1;
}

int execvpe(const char * file, char * const* argv, char * const* envp) {
    char * final_path = find_file(file);
    if (final_path == NULL) return -1;

    ___set_errno(execve(final_path, argv, envp));
    free(final_path);
    return -1;
}

#include <limits.h>
// this is very inefficient
// then again, stack is 1MB so probably fine?
// TODO: maybe rewrite to use alloca, similarly to how glibc does it
int execl(const char * path, const char * arg0, ...) {
    char * argv[ARG_MAX/sizeof(char*)] = {0};
    argv[0] = (char *)arg0;

    va_list args;
    va_start(args, arg0);

    int i = 1;
    while ((argv[i++] = va_arg(args, char *)) != NULL && i < ARG_MAX/sizeof(char*)) {}

    va_end(args);
    return execv(path, argv);
}
int execle(const char * path, const char * arg0, ...) {
    char * argv[ARG_MAX/sizeof(char*)] = {0};
    argv[0] = (char *)arg0;

    va_list args;
    va_start(args, arg0);

    int i = 1;
    while ((argv[i++] = va_arg(args, char *)) != NULL && i < ARG_MAX/sizeof(char*)) {}

    if (i == ARG_MAX/sizeof(char*)) while (va_arg(args, char *) != NULL) {}

    char * const * envp = va_arg(args, char * const *);
    va_end(args);

    return execve(path, argv, envp);
}
int execlp(const char * file, const char * arg0, ...) {
    char * argv[ARG_MAX/sizeof(char*)] = {0};
    argv[0] = (char *)arg0;

    va_list args;
    va_start(args, arg0);

    int i = 1;
    while ((argv[i++] = va_arg(args, char *)) != NULL && i < ARG_MAX/sizeof(char*)) {}

    va_end(args);
    return execvp(file, argv);
}
