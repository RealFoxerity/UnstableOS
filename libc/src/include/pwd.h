#ifndef _PWD_H
#define _PWD_H

#include <sys/types.h>

// passwd and gecos are extensions,
// however we're using the standard /etc/passwd format so we need them
struct passwd {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};
void endpwent();
struct passwd *getpwent();
void setpwent();
struct passwd *getpwnam(const char *name);
int getpwnam_r(const char *name, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result);
struct passwd *getpwuid(uid_t uid);
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result);

#endif