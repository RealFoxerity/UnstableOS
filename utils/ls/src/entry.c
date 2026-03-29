#include "../../../libc/src/include/stdio.h"
#include "../../../libc/src/include/dirent.h"
#include "../../../libc/src/include/sys/stat.h"
#include "../../../libc/src/include/fcntl.h"
#include "../../../libc/src/include/stdlib.h"
#include "../../../libc/src/include/string.h"
#include "../../../libc/src/include/time.h"
#include <stddef.h>

#define COLOR_DIR "\e[36m"
#define COLOR_FILE "\e[37m"
#define COLOR_EXEC "\e[32m"
#define COLOR_SPEC "\e[33m"
#define COLOR_UNKNOWN "\e[31m"
#define COLOR_RESET "\e[0m"

void print_file_info(const struct stat * info, const char * name) {
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

        char * datestr = ctime(&info->st_mtime);
        datestr[strlen(datestr)-1] = '\0';
        printf("\t%lu\t%lu\t%lu\t%lu\t%s ", info->st_nlink, info->st_uid, info->st_gid, info->st_size, datestr);

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
            default:
                printf(COLOR_UNKNOWN);
        }
    } else {
        printf("??????????\t????\t????\t????\t????\t????\t");
    }
    printf("%s"COLOR_RESET"\n", name);
}

int main(int argc, char ** argv) {
    char * path = NULL;
    if (argc < 2) {
        path = ".";
    } else {
        path = argv[1];
    }
    int dirfd = open(path, O_RDONLY, 0);

    struct stat file_info = {0};
    int ret = 0;
    if ((ret = fstat(dirfd, &file_info)) < 0) {
        printf("ls: cannot access %s, errno %d\n", path, ret);
        return 1;
    }

    if (S_ISREG(file_info.st_mode)) {
        print_file_info(&file_info, path);
        return 0;
    }

    DIR * this = NULL;
    if ((this = opendir(path)) == NULL) {
        printf("ls: cannot access %s, errno %d\n", path, ret);
        return 1;
    }
    struct dirent * entry = readdir(this);
    while (entry != NULL) {
        if (fstatat(dirfd, entry->d_name, &file_info, 0) < 0)
            print_file_info(NULL, entry->d_name);
        else
            print_file_info(&file_info, entry->d_name);
        entry = readdir(this);
    }
    closedir(this);
    return 0;
}