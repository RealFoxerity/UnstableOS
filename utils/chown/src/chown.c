#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

const char opts[] = "RHLPh";


int chown_file(int parent_fd, const char * name, int recurse, uid_t owner, gid_t group) {
    if (fchownat(parent_fd, name, owner, group, 0) < 0) {
        fprintf(stderr, "chown: failed to set ownership of '%s': %s\n", name, strerror(errno));
        return 0;
    }

    struct stat buf;
    if (fstatat(parent_fd, name, &buf, 0) >= 0) {
        if ((owner != (uid_t)-1 && buf.st_uid != owner) ||
            (group != (gid_t)-1 && buf.st_gid != group)) {
            fprintf(stderr, "chown: warn: ownership not completely set on '%s'\n", name);
        }
    }

    if (!recurse || !S_ISDIR(buf.st_mode))
        return 1;

    int dirfd = openat(parent_fd, name, O_DIRECTORY | O_RDONLY);
    if (dirfd < 0) {
        err:
        fprintf(stderr, "chown: error while recursing into '%s': %s\n", name, strerror(errno));
        return 0;
    }

    DIR * dir = fdopendir(dirfd);
    if (!dir) {
        close(dirfd);
        goto err;
    }

    struct dirent * dent;
    errno = 0;
    while ((dent = readdir(dir))) {
        if (strcmp(dent->d_name, "." ) == 0)
            continue;
        if (strcmp(dent->d_name, "..") == 0)
            continue;

        if (!chown_file(dirfd, dent->d_name, recurse, owner, group))
            fprintf(stderr, "chown: while recursing into '%s'\n", name);
    }
    closedir(dir);
    if (errno)
        goto err;
    return 1;
}
void print_help() {
    fprintf(stderr,
"Usage: chown [-RHLPh] owner[:group] file...\n"
"Options:\n"
"\t-R\tRecurse into subdirectories\n"
"\t-HLP\tSymlink handling, for POSIX compat, symlinks not yet implemented\n"
"\t-h\tThis help message\n\n"
"owner and/or group can be either numeric id or name\n"
"in case numeric id exists as a name, the name takes precedence\n");
}
int main(int argc, char ** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-RHLPh] owner[:group] file...\n%s -h for more information\n", argv[0], argv[0]);
        return 1;
    }

    int c = 0;
    char * owner = NULL;
    char recurse = 0;
again:
    // double pass to gather the flags
    while ((c = getopt(argc, argv, opts)) != -1) {
        switch (c) {
            case 'R':
                recurse = 1;
                break;
            case 'H':
            case 'L':
            case 'P':
                //fprintf(stderr, "TODO: implement HLP when symlink support\n");
                break;
            case 'h':
                print_help();
                return 0;
            default:
                fprintf(stderr, "chown: Unknown option '-%c'\n", c);
                return 1;
        }
    }
    if (strcmp(argv[optind - 1], "--") != 0 && argv[optind]) {
        if (!owner)
            owner = argv[optind];
        optind++;
        goto again;
    }
    if (!owner) {
        fprintf(stderr, "chown: Missing ownership argument!\n");
        return 1;
    }

    char * group = strchr(owner, ':');
    if (group) {
        if (!*(group+1)) {
            fprintf(stderr, "chown: Invalid group specifier!\n");
            return 1;
        }
        *group++ = '\0';
    }

    uid_t target_uid = -1;
    gid_t target_gid = -1;

    struct passwd * target_owner = getpwnam(owner);
    if (!target_owner) {
        if (isdigit(owner[0])) {
            char * end;
            target_uid = strtoul(owner, &end, 10);
            if (end && !*end)
                goto ok;
        }
        fprintf(stderr, "chown: invalid/nonexistent user specified: '%s'\n", owner);
        return 1;
    }
    target_uid = target_owner->pw_uid;
ok:

    if (group) {
        struct group * target_group = getgrnam(group);
        if (!target_group) {
            if (isdigit(group[0])) {
                char * end;
                target_gid = strtoul(group, &end, 10);
                if (end && !*end)
                    goto ok2;
            }
            fprintf(stderr, "chown: invalid/nonexistent group specified: '%s'\n", group);
            return 1;
        }
        target_gid = target_group->gr_gid;
    }
    char saw_arg_end = 0;
    char errored = 0;
ok2:

    for (int i = 1; i < argc; i++) {
        if (!saw_arg_end) {
            if (strcmp(argv[i], "--") == 0)
                saw_arg_end = 1;
            if (argv[i][0] == '-')
                continue;
        }
        if (argv[i] == owner)
            continue;

        if (!chown_file(AT_FDCWD, argv[i], recurse, target_uid, target_gid))
            errored = 1;
    }
    return errored;
}