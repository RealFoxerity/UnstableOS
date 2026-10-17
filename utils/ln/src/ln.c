#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

const static char valid_options[] = "fsLPh";

void print_help(const char * name) {
    printf(
"Usage: %1$s [-fs] [-L|-P] source_file target_file\n"
"  or:  %1$s [-fs] [-L|-P] source_file... target_dir\n"
"  or:  %1$s [-fs] [-L|-P] source_file\n"
"Create a link target_file to source_file\n"
"Create links in target_dir to each source_file (basename)\n"
"Create a link to source_file in the current directory\n"
"If target_file is a directory, does form 2\n\n"
"  -f   Remove the destination if exists\n"
"  -s   Create symbolic links instead of hard links\n"
"  -L   If source_file names a symlink, resolve it\n"
"  -P   If source_file names a symlink, don't resolve it\n"
"  -h   Display this help message\n\n"
"-P and -L are mutually exclusive, the last one is used\n"
"-P and -L are ignored if -s is specified\n"
"If neither -P or -L is specified, the default is -P\n", name);
}

int main(int argc, char ** argv) {
    char * execpath = argv[0];
    int c = 0;
    int do_symlink = 0;
    int do_unlink = 0;
    int flag = AT_SYMLINK_NOFOLLOW;

    was_file:
    while ((c = getopt(argc, argv, valid_options)) != -1) {
        switch (c) {
            case 'f':
                do_unlink = 1;
                break;
            case 's':
                do_symlink = 1;
                break;
            case 'L':
                flag = AT_SYMLINK_FOLLOW;
                break;
            case 'P':
                flag = AT_SYMLINK_NOFOLLOW;
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                fprintf(stderr, "Try '%s -h' for help\n", argv[0]);
                return 1;
        }
    }

    if (strcmp(argv[optind - 1], "--") != 0 && argv[optind]) {
        optind++;
        goto was_file;
    }

    int files = 0;
    char saw_arg_end = 0;
    // reorder arguments so that it's easier to use later
    for (int i = 1; i < argc; i++) {
        if (!saw_arg_end && argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0)
                saw_arg_end = 1;
            continue;
        }
        argv[files++] = argv[i];
    }
    argv[files] = NULL;

    if (files == 0) {
        fprintf(stderr, "%s: missing operand\n", execpath);
        return 1;
    }
    int dirfd = AT_FDCWD;
    char pathbuf[PATH_MAX];
    char * base = NULL;
    // 3rd form
    if (files == 1) {
        strcpy(pathbuf, argv[0]);
        base = basename(pathbuf);
        if (do_symlink) {
            if (symlinkat(argv[0], AT_FDCWD, base) < 0) {
                fprintf(stderr, "%s: symlinkat %s -> %s: %s\n", execpath, base, argv[0], strerror(errno));
                return 1;
            }
        } else {
            if (linkat(AT_FDCWD, argv[0], AT_FDCWD, base, flag) < 0) {
                fprintf(stderr, "%s: linkat %s -> %s: %s\n", execpath, base, argv[0], strerror(errno));
                return 1;
            }
        }
        return 0;
    }

    // 1st or 2nd
    if (files > 1) {
        dirfd = open(argv[files - 1], O_WRONLY);
        if (dirfd < 0 && (files > 2 || errno != ENOENT)) {
            fprintf(stderr, "%s: target %s: %s\n", execpath, argv[files - 1], strerror(errno));
            return 1;
        }
        if (dirfd >= 0) {
            struct stat st;
            fstat(dirfd, &st);
            if (!S_ISDIR(st.st_mode)) {
                close(dirfd);
                dirfd = AT_FDCWD;

                if (files > 2) {
                    fprintf(stderr, "%s: target %s: %s\n", execpath, argv[files - 1], strerror(ENOTDIR));
                    return 1;
                }
                if (!do_unlink) {
                    fprintf(stderr, "%s: target %s: %s\n", execpath, argv[files - 1], strerror(EEXIST));
                    return 1;
                }
                if (unlink(argv[files - 1]) < 0) {
                    fprintf(stderr, "%s: can't unlink target %s: %s\n", execpath, argv[files - 1], strerror(errno));
                    return 1;
                }
            }
        }
    }

    // 1st
    if (files == 2) {
        int ret = 0;
        if (do_symlink)
            ret = symlink(argv[0], argv[1]);
        else
            ret = link(argv[0], argv[1]);
        if (ret < 0) {
            fprintf(stderr, "%s: %s -> %s: %s\n", execpath, argv[1], argv[0], strerror(errno));
            return 1;
        }
        return 0;
    }

    // 2nd
    char failed = 0;
    for (int i = 0; i < files - 1; i++) {
        strcpy(pathbuf, argv[i]);
        base = basename(pathbuf);
        if (do_symlink) {
            if (symlinkat(argv[i], dirfd, base) < 0) {
                fprintf(stderr, "%s: symlinkat %s/%s -> %s: %s\n", execpath, argv[files - 1], base, argv[i], strerror(errno));
                failed = 1;
            }
        } else {
            if (linkat(AT_FDCWD, argv[i], dirfd, base, flag) < 0) {
                fprintf(stderr, "%s: linkat %s/%s -> %s: %s\n", execpath, argv[files - 1], base, argv[i], strerror(errno));
                failed = 1;
            }
        }
    }

    return failed;
}