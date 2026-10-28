#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

#define _UTS_FIELD_LEN 65

struct utsname {
    char sysname[_UTS_FIELD_LEN];
    char nodename[_UTS_FIELD_LEN];
    char release[_UTS_FIELD_LEN];
    char version[_UTS_FIELD_LEN];
    char machine[_UTS_FIELD_LEN];
};

int uname(struct utsname *name);
#endif