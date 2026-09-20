#ifndef _GRP_H
#define _GRP_H

#include <sys/types.h>
// passwd is an extension,
// however we're using the standard /etc/group format so we need it
struct group {
    char  *gr_name;
    char  *gr_passwd;
    gid_t  gr_gid;
    char **gr_mem;
};

void endgrent();
struct group *getgrent();
void setgrent();
struct group *getgrnam(const char *name);
int getgrnam_r(const char *name, struct group *grp, char *buffer, size_t bufsize, struct group **result);
struct group *getgrgid(gid_t gid);
int getgrgid_r(gid_t gid, struct group *grp, char *buffer, size_t bufsize, struct group **result);
#endif