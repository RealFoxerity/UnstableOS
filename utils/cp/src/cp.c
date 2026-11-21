#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <libgen.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

static const char * valid_options = "fipHLPRrvxh";

#define NOFOLLOW   0
#define FOLLOW_ARG 1
#define FOLLOW_ALL 2

char follow_type = NOFOLLOW;
char recurse = 0;
char unlink_dest = 0;
char interactive = 0;
char preserve_attrs = 0;
char verbose = 0;
char xdev = 0;
char * dest = NULL;
unsigned short mask;
static const char help_message[] =
"Usage: cp [-fipHLP]    source    destination\n"
" or:   cp [-fipHLP]    source... directory\n"
" or:   cp -R [-fipHLP] source... directory\n"
"Copies files and folders from source to destination\n\n"
"Options:\n"
"  -f   Remove the destination file if it can't be opened\n"
"  -i   Prompt for deletion confirmation\n"
"  -p   Preserve attributes (mtime, atime, uid, gid, perms)\n"
"  -H   Follow symlinks only if they are arguments\n"
"  -L   Follow all symlinks\n"
"  -P   Do not follow symlinks\n"
"  -R   Recursively copy directories\n"
"  -r   ...\n"
"  -v   Show files as they're being copied\n"
"  -x   Don't cross filesystem boundaries\n"
"  -h   Display this help message\n\n"
"By default -P is applied\n";

char srcpath[PATH_MAX];
char dstpath[PATH_MAX];

int prompt_user(char * path, char * act) {
    fprintf(stderr, "The file '%s' already exists, do you want to %s it? Y/[N]: ", path, act);
    fflush(stderr);
    int c;
    while (isspace((c = fgetc(stdin)))) {}
    if (tolower(c) == 'y')
        return 1;
    if (tolower(c) == 'n')
        return 0;
    fprintf(stderr, "(Assumed no)\n");
    return 0;
}
#define CPBUFSIZ 8192
int copy_file(int srcfd, int dstfd) {
    static unsigned char copybuf[CPBUFSIZ];
    size_t bufsiz;
    struct stat st;
    if (fstat(dstfd, &st) < 0 || !st.st_blksize || st.st_blksize > CPBUFSIZ)
        bufsiz = CPBUFSIZ;
    else
        bufsiz = st.st_blksize;

    ssize_t read_bytes;
    while ((read_bytes = read(srcfd, copybuf, bufsiz)) > 0) {
        size_t off = 0;
        while (read_bytes > 0) {
            ssize_t written_bytes = write(dstfd, copybuf + off, read_bytes);
            if (written_bytes < 0)
                return 1;
            read_bytes -= written_bytes;
            off += written_bytes;
        }
    }
    if (read_bytes < 0)
        return 1;
    return 0;
}

char linkbuf[SYMLINK_MAX+1];

int do_copy(int basefd, const char * file, int dest_folder_fd, const char * dest_file, unsigned long fsid) {
    if (verbose)
        fprintf(stderr, "%s -> %s\n", srcpath, dstpath);
    struct stat st, st2;
    int flag = (follow_type == FOLLOW_ALL ||
            (follow_type == FOLLOW_ARG && basefd == AT_FDCWD)) ?
            0 : AT_SYMLINK_NOFOLLOW;

    if (fstatat(basefd, file, &st, flag) < 0) {
        general_src_error:
        fprintf(stderr, "cp: %s: %s\n", srcpath, strerror(errno));
        return 1;
    }
    if (fsid == -1)
        fsid = st.st_fsid;
    if (xdev && fsid != st.st_fsid)
        return 0;

    switch (st.st_mode & S_IFMT) {
        case S_IFREG:
        case S_IFDIR:
        case S_IFIFO:
        case S_IFLNK:
        case S_IFCHR:
        case S_IFBLK:
        //case S_IFSOCK:
            break;
        default:
            fprintf(stderr, "cp: %s: Unknown file type\n", srcpath);
            return 1;
    }

    if (S_ISDIR(st.st_mode) && dest_file) {
        fprintf(stderr, "cp: %s: %s\n", dstpath, strerror(ENOTDIR));
        return 1;
    }

    if (S_ISDIR(st.st_mode) && !recurse) {
        fprintf(stderr, "cp: %s: '-R' not specified, skipping\n", srcpath);
        return 0;
    }

    int srcfd = -1;
    if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) {
        srcfd = openat(basefd, file, O_RDONLY | (S_ISDIR(st.st_mode) ? O_DIRECTORY : 0));
        if (srcfd < 0)
            goto general_src_error;

        // double check against races
        // we want the fd because that's "immune" against unlinks, renames, mounts, umounts, ...
        fstat(srcfd, &st);
        if (xdev && fsid != st.st_fsid) {
            close(srcfd);
            return 0;
        }
        if (S_ISDIR(st.st_mode) && dest_file) {
            fprintf(stderr, "cp: %s: %s\n", dstpath, strerror(ENOTDIR));
            close(srcfd);
            return 1;
        }

        if (S_ISDIR(st.st_mode) && !recurse) {
            fprintf(stderr, "cp: %s: '-R' not specified, skipping\n", srcpath);
            close(srcfd);
            return 0;
        }
    }

    static char dupfile[PATH_MAX];
    strcpy(dupfile, file);
    char * base = basename(dupfile);
    char is_unnamed = base ? !strcmp(base, "/") || !strcmp(base, "//") || !strcmp(base, ".") || !strcmp(base, "..") : 0;
    dest_file = dest_file ? dest_file : base;

    int ok;
    if (!is_unnamed && ((ok = fstatat(dest_folder_fd, dest_file, &st2, flag)) == 0 || errno != ENOENT)) {
        if (ok != 0) {
            general_dst_error:
            fprintf(stderr, "cp: %s: %s\n", dstpath, strerror(errno));
            close(srcfd);
            return 1;
        }

        // references the same file
        if (st.st_fsid == st2.st_fsid && st.st_ino == st2.st_ino) {
            //fprintf(stderr, "cp: %s and %s reference the same file, skipping\n", srcpath, dstpath);
            close(srcfd);
            return 0;
        }

        if ((!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) || st.st_mode & S_IFMT != st2.st_mode & S_IFMT) {
            if (S_ISDIR(st2.st_mode)) {
                fprintf(stderr, "cp: %s: %s\n", dstpath, strerror(EISDIR));
                close(srcfd);
                return 1;
            }
            if (interactive && !prompt_user(dstpath, "remove")) {
                fprintf(stderr, "cp: %s: %s\n", dstpath, strerror(EEXIST));
                close(srcfd);
                return 1;
            }
            if (unlinkat(dest_folder_fd, dest_file, S_ISDIR(st2.st_mode) ? AT_REMOVEDIR : 0) < 0)
                goto general_dst_error;
        } else if (!S_ISDIR(st.st_mode) && interactive && !prompt_user(dstpath, "overwrite")) {
            fprintf(stderr, "cp: %s: %s\n", dstpath, strerror(EEXIST));
            close(srcfd);
            return 1;
        }
    }

    if (is_unnamed)
        assert(S_ISDIR(st.st_mode));

    int dstfd = -1;
    DIR * dir;
    struct dirent * dent;
    int ret = 0;
    switch (st.st_mode & S_IFMT) {
        case S_IFREG:
            // AT_SYMLINK_NOFOLLOW in case we raced with something else
            dstfd = openat(dest_folder_fd, dest_file, O_WRONLY | O_TRUNC | O_CREAT, st.st_mode & 07777);
            if (dstfd < 0) {
                if (!unlink_dest || unlinkat(dest_folder_fd, dest_file, 0) < 0)
                    goto general_dst_error;
                dstfd = openat(dest_folder_fd, dest_file, O_WRONLY | O_TRUNC | O_CREAT, st.st_mode & 07777);
                if (dstfd < 0)
                    goto general_dst_error;
            }
            ret = copy_file(srcfd, dstfd);
            if (ret)
                fprintf(stderr, "cp: Warning: %s: %s\n", srcpath, strerror(errno));
            close(srcfd);
            close(dstfd);
            break;
        case S_IFDIR:
            // our openat acts as mkdir and we need the fd as soon as possible
            if (!is_unnamed &&
                (dstfd = openat(dest_folder_fd, dest_file, O_WRONLY | O_DIRECTORY | O_CREAT,
                (st.st_mode & ~(preserve_attrs ? 0 : mask)) | S_IRWXU)) < 0)
            {
                if (errno == EEXIST) // write into
                    dstfd = openat(dest_folder_fd, dest_file, O_WRONLY | O_DIRECTORY);
                if (dstfd < 0)
                    goto general_dst_error;
            }

            dir = fdopendir(srcfd);
            if (!dir) {
                int _err = errno;
                close(dstfd);
                errno = _err;
                goto general_src_error;
            }
            char * sbuf = strchrnul(srcpath, '\0');
            char * dbuf = strchrnul(dstpath, '\0');

            errno = 0;
            while ((dent = readdir(dir)) != NULL) {
                errno = 0;
                if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
                    continue;

                if (sbuf[-1] != '/')
                    strncat(srcpath, "/", PATH_MAX - (sbuf - srcpath) - 1);
                strncat(srcpath, dent->d_name, PATH_MAX - (sbuf - srcpath) - 1 - 1);
                if (dbuf[-1] != '/')
                    strncat(dstpath, "/", PATH_MAX - (dbuf - dstpath) - 1);
                strncat(dstpath, dent->d_name, PATH_MAX - (dbuf - dstpath) - 1 - 1);

                if (do_copy(srcfd, dent->d_name, dstfd == -1 ? dest_folder_fd : dstfd, NULL, fsid))
                    ret = 1;

                *dbuf = '\0';
                *sbuf = '\0';
                errno = 0;
            }
            int _err = errno;
            closedir(dir);
            close(dstfd);
            errno = _err;
            if (errno)
                goto general_src_error;
            break;
        case S_IFIFO:
            if (mkfifoat(dest_folder_fd, dest_file, st.st_mode & ~(preserve_attrs ? 0 : mask)) < 0)
                goto general_dst_error;
            break;
        case S_IFLNK:
            ret = readlinkat(basefd, file, linkbuf, SYMLINK_MAX);
            if (ret < 0)
                goto general_src_error;
            linkbuf[ret] = '\0';
            if (symlinkat(linkbuf, dest_folder_fd, dest_file) < 0)
                goto general_dst_error;
            ret = 0;
            break;
        case S_IFCHR:
        case S_IFBLK:
            if (mknodat(dest_folder_fd, dest_file, st.st_mode, st.st_rdev) < 0)
                goto general_dst_error;
            break;
        //case S_IFSOCK:

        default:break;
    }

    if (!preserve_attrs)
        return ret;

    struct timespec times[2] = {{.tv_sec = st.st_atime}, {.tv_sec = st.st_mtime}};
    if (utimensat(dest_folder_fd, dest_file, times, AT_SYMLINK_NOFOLLOW) < 0) {
        fprintf(stderr, "cp: Failed to preserve times of %s: %s\n", srcpath, strerror(errno));
        ret++;
    }

    if (fchownat(dest_folder_fd, dest_file, st.st_uid, st.st_gid, AT_SYMLINK_NOFOLLOW) < 0) {
        fprintf(stderr, "cp: Failed to preserve ownership of %s: %s\n", srcpath, strerror(errno));
        ret++;
    }

    if (fchmodat(dest_folder_fd, dest_file, st.st_mode, AT_SYMLINK_NOFOLLOW) < 0) {
        fprintf(stderr, "cp: Failed to preserve permissions of %s: %s\n", srcpath, strerror(errno));
        ret++;
    }

    return ret > 0;
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        usage:
        fprintf(stderr, "Usage: %s [-fipHLPRx] source... destination\nTry '%s -h' for help\n", argv[0], argv[0]);
        return 1;
    }
    mask = umask(0);
    while (1) {
        int c;
        while ((c = getopt(argc, argv, valid_options)) != -1) {
            switch (c) {
                case 'f':
                    unlink_dest = 1;
                    break;
                case 'i':
                    interactive = 1;
                    break;
                case 'p':
                    preserve_attrs = 1;
                    break;
                case 'R':
                case 'r':
                    recurse = 1;
                    break;
                case 'H':
                    follow_type = FOLLOW_ARG;
                    break;
                case 'L':
                    follow_type = FOLLOW_ALL;
                    break;
                case 'P':
                    follow_type = NOFOLLOW;
                    break;
                case 'v':
                    verbose = 1;
                    break;
                case 'x':
                    xdev = 1;
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

    char saw_option_end = 0;
    size_t files = 0;
    char **__dest = NULL;
    for (int i = 1; i < argc; i++) {
        if (!saw_option_end && !strcmp(argv[i], "--")) {
            saw_option_end = 1;
            continue;
        }
        if (!saw_option_end && argv[i][0] == '-')
            continue;

        files++;
        dest = argv[i];
        __dest = argv + i;
    }
    if (!__dest || !dest || files == 1)
        goto usage;
    *__dest = NULL;
    if (strlen(dest) >= PATH_MAX) {
        fprintf(stderr, "cp: %s: %s\n", dest, strerror(ENAMETOOLONG));
        return 1;
    }

    strcpy(dstpath, dest);
    char *dbuf = strchrnul(dstpath, '\0');

    struct stat st;
    errno = 0;
    int stat_ok = stat(dest, &st);
    if (stat_ok < 0 && (errno != ENOENT || files > 2)) {
        fprintf(stderr, "cp: stat(%s): %s\n", dest, strerror(errno));
        return 1;
    }
    if (files > 2 && !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "cp: %s: %s\n", dest, strerror(ENOTDIR));
        return 1;
    }

    int dest_fd = open(dest, O_WRONLY | O_DIRECTORY);
    if (dest_fd < 0 && files > 2) {
        fprintf(stderr, "cp: open(%s): %s\n", dest, strerror(errno));
        return 1;
    }

    saw_option_end = 0;
    int errored = 0;
    for (int i = 1; i < argc; i++) {
        if (argv + i == __dest)
            break;
        if (!saw_option_end && !strcmp(argv[i], "--")) {
            saw_option_end = 1;
            continue;
        }
        if (!saw_option_end && argv[i][0] == '-')
            continue;

        strncpy(srcpath, argv[i], PATH_MAX - 1);
        srcpath[PATH_MAX - 1] = '\0';

        if (dest_fd >= 0) {
            char * dupfile = strdup(argv[i]);
            char * base = dupfile ? basename(dupfile) : NULL;
            char is_unnamed = base ? !strcmp(base, "/") || !strcmp(base, "//") || !strcmp(base, ".") || !strcmp(base, "..") : 0;
            if (!is_unnamed) {
                strncat(dstpath, "/", PATH_MAX - (dbuf - dstpath) - 1);
                strncat(dstpath, base ? base : argv[i], PATH_MAX - (dbuf - dstpath) - 1 -1);
            }
            free(dupfile);
        }

        if (do_copy(AT_FDCWD, argv[i],
                    dest_fd < 0 ? AT_FDCWD : dest_fd,
                    dest_fd < 0 ? dest : NULL, -1))
            errored = 1;
        *dbuf = '\0';
    }
    return errored;
}