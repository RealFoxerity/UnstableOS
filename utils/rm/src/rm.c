#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <libgen.h>
#include <assert.h>

// TODO:
/* The rm utility is required to descend to arbitrary depths so that any file hierarchy may be deleted.
 * This means, for example, that the rm utility cannot run out of file descriptors during its descent
 * (that is, if the number of file descriptors is limited, rm cannot be implemented in the historical fashion
 * where one file descriptor is used per directory level). Also, rm is not permitted to fail because of path
 * length restrictions, unless an operand specified by the user is longer than {PATH_MAX}.
 */
// says posix

static const char * valid_options = "fidRrvh";
static char recurse = 0;
static char interactive = 0;
static char force = 0;
static char only_empty = 0;
static char verbose = 0;

static const char help_message[] =
"Usage: rm [-fidRrv] file...\n"
"Removes files\n\n"
"Options:\n"
"  -f   Ignore nonexistent file, don't prompt for read-only files\n"
"  -i   Prompt for each removal\n"
"  -d   Only remove empty directories\n"
"  -R   Recursively remove subdirectories\n"
"  -r   ...\n"
"  -v   Show files as they're being removed\n"
"  -h   Display this help message\n";

static char path[PATH_MAX];

#define RO_FMT "rm: '%s' is read-only, try to remove it anyway? Y/[N]: "
#define I_DIR "rm: descend into and/or remove '%s'? Y/[N]: "
#define I_DEL "rm: remove '%s'? Y/[N]: "

int prompt_user(const char * prompt) {
    fprintf(stderr, prompt, path);
    fflush(stderr);

    fflush(stdin);
    int c = fgetc(stdin);
    if (tolower(c) == 'y')
        return 1;
    if (tolower(c) == 'n')
        return 0;
    fprintf(stderr, "rm: (Assumed no)\n");
    return 0;
}

// most of the ENOENT checks are for races with different rm invocations
int do_remove(int basefd, const char * name) {
    struct stat st;
    if (fstatat(basefd, name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT && force)
            return 0;
        goto err;
    }
    if (S_ISDIR(st.st_mode) && !recurse) {
        errno = EISDIR;
        goto err;
    }
    if (faccessat(basefd, name, W_OK, AT_SYMLINK_NOFOLLOW | AT_EACCESS) < 0) {
        if (errno == ENOENT)
            goto log;
        if (errno != EACCES)
            goto err;
        if (interactive && !prompt_user(RO_FMT))
            return 1;
    } else if (interactive && !prompt_user(S_ISDIR(st.st_mode) ? I_DIR : I_DEL))
            return 1;

    if (unlinkat(basefd, name, 0) >= 0 || errno == ENOENT)
        goto log;
    if (errno != EISDIR || !recurse)
        goto err;



    if (unlinkat(basefd, name, AT_REMOVEDIR) >= 0 || errno == ENOENT)
        goto log;
    // we want to go deeper into the hierarchy even if we can't unlink this specific one
    // of course we will fail on the last unlink (and many before that)
    // but it is more thorough
    if ((errno != ENOTEMPTY && errno != EACCES) || only_empty)
        goto err;

    int fd = openat(basefd, name, O_RDONLY | O_DIRECTORY);
    if (fd < 0 && errno == ENOENT)
        goto log;
    if (fd < 0)
        goto err;

    DIR * dir = fdopendir(fd);
    if (!dir) {
        int e = errno;
        close(fd);
        errno = e;
        goto err;
    }
    char failed = 0;
    char * pathend = strchrnul(path, '\0');
    struct dirent * dent;
    errno = 0;
    off_t last_stable_entry = 0;
    while ((dent = readdir(dir)) != NULL) {
        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) {
            last_stable_entry = dent->d_off;
            continue;
        }

        if (pathend[-1] != '/')
            strncat(path, "/", PATH_MAX - (pathend - path) - 1);
        strncat(path, dent->d_name, PATH_MAX - (pathend - path) - 1 - 1);

        if (do_remove(fd, dent->d_name)) {
            last_stable_entry = dent->d_off;
            failed = 1;
        } else
            // removal might (almost certainly will) move files around
            seekdir(dir, last_stable_entry);
        *pathend = '\0';
        errno = 0;
    }
    int e = errno;
    closedir(dir);

    errno = e;
    if (errno)
        goto err;

    // if we failed even once there's no chance the unlink() will succeed (at least ENOTEMPTY)
    if (failed) {
        // can't know for sure which file got fucked up or how it did, so a somewhat realistic guess here
        errno = ENOTEMPTY;
        goto err;
    }

    if (interactive && !prompt_user(I_DEL))
        return 1;
    if (unlinkat(basefd, name, AT_REMOVEDIR) < 0 && errno != ENOENT)
        goto err;


    log:
    fprintf(stdout, "rm: removed '%s'\n", path);
    return 0;

    err:
    fprintf(stderr, "rm: '%s': %s\n", path, strerror(errno));
    return 1;
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        usage:
        fprintf(stderr, "Usage: %s [-fdiRrv] file...\nTry '%s -h' for help\n", argv[0], argv[0]);
        return 1;
    }

    while (1) {
        int c;
        while ((c = getopt(argc, argv, valid_options)) != -1) {
            switch (c) {
                case 'f':
                    force = 1;
                    interactive = 0;
                    break;
                case 'i':
                    force = 0;
                    interactive = 1;
                    break;
                case 'R':
                case 'r':
                    recurse = 1;
                    break;
                case 'd':
                    only_empty = 1;
                    break;
                case 'v':
                    verbose = 1;
                    break;
                case 'h':
                    printf("%s", help_message);
                    return 0;
                default:
                    fprintf(stderr, "Try '%s -h' for help\n", argv[0]);
                    return 1;
            }
        }

        if (!strcmp(argv[optind - 1], "--") || !argv[optind])
            break;
        optind++;
    }

    if (recurse)
        only_empty = 0;

    char saw_option_end = 0;
    char saw_arg = 0;
    int errored = 0;
    for (int i = 1; i < argc; i++) {
        if (!saw_option_end && !strcmp(argv[i], "--")) {
            saw_option_end = 1;
            continue;
        }
        if (!saw_option_end && argv[i][0] == '-')
            continue;
        saw_arg = 1;

        char * dupfile = strdup(argv[i]);
        char * base = dupfile ? basename(dupfile) : NULL;
        char is_unnamed = base ? !strcmp(base, ".") || !strcmp(base, "..") : 0;
        free(dupfile);
        if (is_unnamed) {
            fprintf(stderr, "rm: refusing to remove '.' or '..', skipping %s\n", argv[i]);
            errored = 1;
            continue;
        }

        strncpy(path, argv[i], PATH_MAX - 1);
        path[PATH_MAX - 1] = '\0';

        if (do_remove(AT_FDCWD, argv[i]))
            errored = 1;
    }

    if (!saw_arg && !force) {
        fprintf(stderr, "rm: missing operand\n");
        goto usage;
    }

    return errored;
}