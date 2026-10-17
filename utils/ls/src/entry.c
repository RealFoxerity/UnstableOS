#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include <unistd.h>

#define COLOR_DIR "\e[36m"
#define COLOR_FILE "\e[37m"
#define COLOR_EXEC "\e[32m"
#define COLOR_SPEC "\e[33m"
#define COLOR_LINK "\e[96m"
#define COLOR_UNKNOWN "\e[31m"
#define COLOR_RESET "\e[0m"

int current_year;

void print_file_info(const struct stat * info, const char * name, int dirfd) {
    if (info != NULL) {
        switch (info->st_mode & S_IFMT) {
            case S_IFREG:
                printf("-");
                break;
            case S_IFDIR:
                printf("d");
                break;
            case S_IFBLK:
                printf("b");
                break;
            case S_IFCHR:
                printf("c");
                break;
            case S_IFLNK:
                printf("l");
                break;
            default:
                printf("?");
        }

        if (info->st_mode & S_IRUSR) printf("r");
        else printf("-");

        if (info->st_mode & S_IWUSR) printf("w");
        else printf("-");

        if (info->st_mode & S_IXUSR) {
            if (info->st_mode & S_ISUID)
                printf("s");
            else
                printf("x");
        } else {
            if (info->st_mode & S_ISUID)
                printf("S");
            else
                printf("-");
        }


        if (info->st_mode & S_IRGRP) printf("r");
        else printf("-");

        if (info->st_mode & S_IWGRP) printf("w");
        else printf("-");

        if (info->st_mode & S_IXGRP) {
            if (info->st_mode & S_ISGID)
                printf("s");
            else
                printf("x");
        } else {
            if (info->st_mode & S_ISGID)
                printf("S");
            else
                printf("-");
        }


        if (info->st_mode & S_IROTH) printf("r");
        else printf("-");

        if (info->st_mode & S_IWOTH) printf("w");
        else printf("-");

        if (info->st_mode & S_IXOTH) {
            if (info->st_mode & S_ISVTX)
                printf("s");
            else
                printf("x");
        } else {
            if (info->st_mode & S_ISVTX)
                printf("S");
            else
                printf("-");
        }

        struct tm * tm = localtime(&info->st_mtime);
        char datestr[128];
        strftime(datestr, 128,
            tm->tm_year == current_year ?
                "%b %d %H:%M" :
                "%b %d  %4Y",
            tm);
        printf(" %3lu %5lu %5lu %8lld %s ", info->st_nlink, info->st_uid, info->st_gid, info->st_size, datestr);

        static char symlink_target[SYMLINK_MAX+1];
        symlink_target[0] = '\0';
        switch (info->st_mode & S_IFMT) {
            case S_IFREG:
                if (info->st_mode & S_IXUSR ||
                    info->st_mode & S_IXGRP ||
                    info->st_mode & S_IXOTH)
                    printf(COLOR_EXEC);
                else
                    printf(COLOR_FILE);
                break;
            case S_IFDIR:
                printf(COLOR_DIR);
                break;
            case S_IFBLK:
                printf(COLOR_SPEC);
                break;
            case S_IFCHR:
                printf(COLOR_SPEC);
                break;
            case S_IFLNK:
                printf(COLOR_LINK);
                ssize_t ret = readlinkat(dirfd, name, symlink_target, SYMLINK_MAX);
                if (ret < 0)
                    strcpy(symlink_target, "???");
                else
                    symlink_target[ret] = '\0';
                break;
            default:
                printf(COLOR_UNKNOWN);
        }
        printf("%s%s%s"COLOR_RESET"\n", name, S_ISLNK(info->st_mode) ? " -> " : "", symlink_target);
        return;
    }

    printf("??????????\t????\t????\t????\t????\t????\t");
    printf("%s"COLOR_RESET"\n", name);
}

int main(int argc, char ** argv) {
    char * path = NULL;
    if (argc < 2) {
        path = ".";
    } else {
        path = argv[1];
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t realtime = ts.tv_sec;
    struct tm * tm = localtime(&realtime);
    current_year = tm->tm_year;

    int dirfd = open(path, O_RDONLY, 0);
    if (dirfd < 0) {
        perror("ls: open()");
        return 1;
    }
    struct stat file_info = {0};
    int ret = 0;
    if ((ret = fstat(dirfd, &file_info)) < 0) {
        perror("ls: fstat()");
        return 1;
    }

    if (S_ISREG(file_info.st_mode)) {
        print_file_info(&file_info, path, dirfd);
        return 0;
    }

    DIR * this = NULL;
    if ((this = opendir(path)) == NULL) {
        perror("ls: opendir()");
        return 1;
    }
    struct dirent * entry = readdir(this);
    while (entry != NULL) {
        if (fstatat(dirfd, entry->d_name, &file_info, AT_SYMLINK_NOFOLLOW) < 0)
            print_file_info(NULL, entry->d_name, dirfd);
        else
            print_file_info(&file_info, entry->d_name, dirfd);
        entry = readdir(this);
    }
    closedir(this);
    return 0;
}