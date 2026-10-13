#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(const int argc, char *argv[]) {
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [login]\n", argv[0]);
        return 1;
    }
    const struct passwd *pwd = NULL;
    if (argc == 2) {
        const char *login = argv[1];
        if (login == NULL || login[0] == '\0') {
            fprintf(stderr, "Usage: %s [login]\n", argv[0]);
            return 1;
        }
        pwd = getpwnam(login);
        if (pwd == NULL) {
            fprintf(stderr, "%s: No such user '%s'\n", argv[0], login);
            return 1;
        }
    } else {
        const uid_t uid = getuid();
        pwd = getpwuid(uid);
        if (pwd == NULL) {
            perror("getpwuid()");
            return 1;
        }
    }

    setgrent();
    struct group *grp;
    int groups = 0;

    while ((grp = getgrent()) != NULL) {
        for (int i = 0; grp->gr_mem[i] != NULL; i++) {
            if (strcmp(grp->gr_mem[i], pwd->pw_name) == 0) {
                groups++;
                if (groups == 1) {
                    printf("%s", grp->gr_name);
                } else {
                    printf(" %s", grp->gr_name);
                }
                break;
            }
        }
    }
    endgrent();

    printf("\n");

    return 0;
}
