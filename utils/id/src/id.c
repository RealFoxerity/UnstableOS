#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <limits.h>

const char valid_options[] = "Ggnruh";

void print_help() {
    fprintf(stderr,
"Usage: id [OPTIONS] [USER]\n"
"Print information about users and groups\n\n"
"\t-G\tPrint all group IDs (rgid, egid, supplementary)\n"
"\t-g\tPrint only egid\n"
"\t-n\tPrint names instead of ids\n"
"\t-u\tPrint only the euid\n"
"\t-r\tSet -G, -g, -u to real ids\n"
"\t-h\tPrint this help message\n\n"
"By default print euid, egid, supplementary groups, and their names\n");
}

int main(int argc, char ** argv) {
    int c = 0;

    char print_groups = 0;
    char print_gid  = 0;
    char print_names = 0;
    char print_real = 0;
    char print_uid = 0;
    char * user = NULL;
    was_user:
    while ((c = getopt(argc, argv, valid_options)) != -1) {
        switch (c) {
            case 'G':
                if (print_gid || print_uid) {
                    fprintf(stderr, "%s: -G, -g, and -u are mutually exclusive\n", argv[0]);
                    return 1;
                }
                print_groups = 1;
                break;
            case 'g':
                if (print_groups || print_uid) {
                    fprintf(stderr, "%s: -G, -g, and -u are mutually exclusive\n", argv[0]);
                    return 1;
                }
                print_gid = 1;
                break;
            case 'n':
                print_names = 1;
                break;
            case 'r':
                print_real = 1;
                break;
            case 'u':
                if (print_gid || print_groups) {
                    fprintf(stderr, "%s: -G, -g, and -u are mutually exclusive\n", argv[0]);
                    return 1;
                }
                print_uid = 1;
                break;
            case 'h':
                print_help();
                return 0;
            default:
                fprintf(stderr, "%s: Invalid option '%c'\nTry '%s -h' for help\n", argv[0], c, argv[0]);
                return 1;
        }
    }

    if (user && argv[optind]) {
        fprintf(stderr, "%s: Invalid option '%s'\nTry '%s -h' for help\n", argv[0], argv[optind], argv[0]);
        return 1;
    }
    if (argv[optind]) {
        user = argv[optind++];
        goto was_user;
    }

    struct passwd * pwd = NULL;
    struct group * grp;

    uid_t uid, euid;
    gid_t gid, egid;

    char user_supplied = user ? 1 : 0;
    if (user) {
        pwd = getpwnam(user);
        if (pwd == NULL) {
            fprintf(stderr, "%s: No such user '%s'\n", argv[0], user);
            return 1;
        }
        grp = getgrgid(pwd->pw_gid);
        if (grp == NULL) {
            fprintf(stderr, "%s: No primary group record for user '%s'\n", argv[0], user);
            return 1;
        }

        uid = pwd->pw_uid;
        euid = pwd->pw_uid;
        gid = pwd->pw_gid;
        egid = pwd->pw_gid;
    } else {
        uid = getuid();
        euid = geteuid();
        gid = getgid();
        egid = getegid();

        pwd = getpwuid(uid);
        //if (pwd == NULL) {
        //    fprintf(stderr, "%s: No user record for current uid (%d)\n", argv[0], uid);
        //    return 1;
        //}
        user = pwd ? pwd->pw_name : NULL;

        grp = getgrgid(gid);
        //if (grp == NULL) {
        //    fprintf(stderr, "%s: No group record for current gid (%d)\n", argv[0], gid);
        //    return 1;
        //}
    }

    if (print_uid) {
        uid_t target_uid = print_real ? uid : euid;
        if (uid != target_uid) { // we process ruid by default
            pwd = getpwuid(target_uid);
            //if (pwd == NULL) {
            //    fprintf(stderr, "%s: No user record for uid (%d)\n", argv[0], target_uid);
            //    return 1;
            //}
        }
        if (print_names && pwd)
            printf("%s\n", pwd->pw_name);
        else
            printf("%d\n", target_uid);
        return 0;
    }

    if (print_gid) {
        gid_t target_gid = print_real ? gid : egid;
        if (gid != target_gid) {
            grp = getgrgid(target_gid);
            //if (grp == NULL) {
            //    fprintf(stderr, "%s: No group record for gid (%d)\n", argv[0], target_gid);
            //    return 1;
            //}
        }
        if (print_names && grp)
            printf("%s\n", grp->gr_name);
        else
            printf("%d\n", target_gid);
        return 0;
    }

    if (print_groups) {
        if (print_names && grp)
            printf("%s", grp->gr_name);
        else
            printf("%d", gid);

        if (gid != egid) {
            if (print_names) {
                grp = getgrgid(egid);
                //if (grp == NULL) {
                //    fprintf(stderr, "%s: No group record for gid (%d)\n", argv[0], egid);
                //    return 1;
                //}
                if (grp)
                    printf(" %s", grp->gr_name);
                else
                    printf(" %d", egid);
            } else
                printf(" %d", egid);
        }

        if (user_supplied) {
            while ((grp = getgrent())) {
                if (grp->gr_gid == gid || grp->gr_gid == egid)
                    continue;
                for (int i = 0; grp->gr_mem[i]; i++) {
                    if (strcmp(grp->gr_mem[i], user) == 0) {
                        if (print_names)
                            printf(" %s", grp->gr_name);
                        else
                            printf(" %d", grp->gr_gid);
                        break;
                    }
                }
            }
        } else {
            gid_t groups[NGROUPS_MAX];
            int read = getgroups(NGROUPS_MAX, groups);
            for (int i = 0; i < read; i++) {
                if (print_names) {
                    grp = getgrgid(groups[i]);
                    //if (grp == NULL) {
                    //    fprintf(stderr, "%s: No group record for gid (%d)\n", argv[0], groups[i]);
                    //    return 1;
                    //}
                    if (grp)
                        printf(" %s", grp->gr_name);
                    else
                        printf(" %d", groups[i]);
                } else
                    printf(" %d", groups[i]);
            }
        }
        printf("\n");
        return 0;
    }


    printf("uid=%d", uid);
    if (user)
        printf("(%s)", user);
    if (uid != euid) {
        pwd = getpwuid(euid);
        //if (pwd == NULL) {
        //    fprintf(stderr, "%s: No user record for uid (%d)\n", argv[0], euid);
        //    return 1;
        //}
        printf(" euid=%d", euid);
        if (pwd && pwd->pw_name)
            printf("(%s)", pwd->pw_name);
    }
    printf(" gid=%d", uid);
    if (grp && grp->gr_name)
        printf("(%s)", grp->gr_name);
    if (gid != egid) {
        grp = getgrgid(egid);
        //if (pwd == NULL) {
        //    fprintf(stderr, "%s: No user record for uid (%d)\n", argv[0], euid);
        //    return 1;
        //}
        printf(" egid=%d", egid);
        if (grp && grp->gr_name)
            printf("(%s)", grp->gr_name);
    }
    char printed_header = 0;
    if (user_supplied) {
        while ((grp = getgrent())) {
            if (grp->gr_gid == gid)
                continue;
            for (int i = 0; grp->gr_mem[i]; i++) {
                if (strcmp(grp->gr_mem[i], user) == 0) {
                    if (!printed_header) {
                        printf(" groups=%d", grp->gr_gid);
                        printed_header = 1;
                    } else
                        printf(",%d", grp->gr_gid);
                    printf("(%s)", grp->gr_name);
                    break;
                }
            }
        }
    } else {
        gid_t groups[NGROUPS_MAX];
        int read = getgroups(NGROUPS_MAX, groups);
        for (int i = 0; i < read; i++) {
            grp = getgrgid(groups[i]);
            //if (grp == NULL) {
            //    fprintf(stderr, "%s: No group record for gid (%d)\n", argv[0], groups[i]);
            //    return 1;
            //}
            if (!printed_header) {
                printf(" groups=%d", grp->gr_gid);
                printed_header = 1;
            } else
                printf(",%d", grp->gr_gid);
            printf("(%s)", grp->gr_name);
        }
    }
    printf("\n");
    return 0;
}