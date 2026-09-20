#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <errno.h>

static int __getpwent_r(FILE * f, struct passwd *gr, char ** line, size_t * n, struct passwd **result) {
    *result = NULL;
    ssize_t read = 0;
    int old_errno = ___get_errno();
    ___set_errno(0);
    while (1) {
        read = getline(line, n, f);

        if (read <= 1) {
            *result = NULL;
            int err = ___get_errno();
            ___set_errno(old_errno);
            return feof(f) ? 0 : -err;
        }

        if ((*line)[read - 1] == '\n') // for example a missing trailing newline
            (*line)[read - 1] =  '\0';

        char * s = *line;
        gr->pw_name = s;

        if (!((s = strchr(s, ':')))) continue;
        *s++ = '\0';
        gr->pw_passwd = s;

        if (!((s = strchr(s, ':')))) continue;
        *s++ = '\0';
        gr->pw_uid = strtoll(s, &s, 10);

        if (*s != ':') continue;
        *s++ = '\0';
        gr->pw_gid = strtoll(s, &s, 10);

        if (*s != ':') continue;
        *s++ = '\0';
        gr->pw_gecos = s;

        if (!((s = strchr(s, ':')))) continue;
        *s++ = '\0';
        gr->pw_dir = s;

        if (!((s = strchr(s, ':')))) continue;
        *s++ = '\0';
        gr->pw_shell = s;
        break;
    }
    *result = gr;
    return read;
}

static FILE * f = NULL;

static char * pwbuf = NULL;
static size_t pwbuflen = 0;
static struct passwd pw;

// not really sure if this is valid, but getpwent and getpwnam will be mutually unsafe

void endpwent() {
    if (f) fclose(f);
    f = NULL;
}
struct passwd *getpwent() {
    if (!f) {
        f = fopen("/etc/passwd", "r");
        if (!f)
            return NULL;
    }
    struct passwd *res;
    int ret = __getpwent_r(f, &pw, &pwbuf, &pwbuflen, &res);
    if (ret < 0)
        ___set_errno(-ret);

    return res;
}
void setpwent() {
    if (f) rewind(f);
}

struct passwd *getpwnam(const char *name) {
    FILE * passwd = fopen("/etc/passwd", "r");
    if (!passwd)
        return NULL;
    struct passwd *res = NULL;
    while (1) {
        int ret = __getpwent_r(passwd, &pw, &pwbuf, &pwbuflen, &res);
        if (ret < 0)
            ___set_errno(-ret);
        if (ret <= 0 || !res)
            break;
        if (pw.pw_name && strcmp(name, pw.pw_name) == 0)
            break;
    }
    fclose(passwd);
    return res;
}
int getpwnam_r(const char *name, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result) {
    char * line = NULL;
    size_t n = 0;

    int old_errno = ___get_errno();
    FILE * passwd = fopen("/etc/passwd", "r");
    if (!passwd) {
        int err = ___get_errno();
        ___set_errno(old_errno);
        return err;
    }

    while (1) {
        int read = __getpwent_r(passwd, pwd, &line, &n, result);
        if (read < 0) {
            free(line);
            fclose(passwd);
            return -read;
        }
        if (!*result)
            break;
        if (read > bufsize) {
            free(line);
            fclose(passwd);
            return ERANGE;
        }
        if (pwd->pw_name && strcmp(name, pwd->pw_name) == 0) {
            memcpy(buffer, line, read);
            pwd->pw_name   = buffer + (pwd->pw_name   - line);
            pwd->pw_passwd = buffer + (pwd->pw_passwd - line);
            pwd->pw_gecos  = buffer + (pwd->pw_gecos  - line);
            pwd->pw_dir    = buffer + (pwd->pw_dir    - line);
            pwd->pw_shell  = buffer + (pwd->pw_shell  - line);
            break;
        }
    }
    free(line);
    fclose(passwd);
    return 0;
}

struct passwd *getpwuid(uid_t uid) {
    FILE * passwd = fopen("/etc/passwd", "r");
    if (!passwd)
        return NULL;
    struct passwd *res = NULL;
    while (1) {
        int ret = __getpwent_r(passwd, &pw, &pwbuf, &pwbuflen, &res);
        if (ret < 0)
            ___set_errno(-ret);
        if (ret <= 0 || !res)
            break;
        if (pw.pw_uid == uid)
            break;
    }
    fclose(passwd);
    return res;
}
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result) {
    char * line = NULL;
    size_t n = 0;

    int old_errno = ___get_errno();
    FILE * passwd = fopen("/etc/passwd", "r");
    if (!passwd) {
        int err = ___get_errno();
        ___set_errno(old_errno);
        return err;
    }

    while (1) {
        int read = __getpwent_r(passwd, pwd, &line, &n, result);
        if (read < 0) {
            free(line);
            fclose(passwd);
            return -read;
        }
        if (!*result)
            break;
        if (read > bufsize) {
            free(line);
            fclose(passwd);
            return ERANGE;
        }
        if (pwd->pw_uid == uid) {
            memcpy(buffer, line, read);
            pwd->pw_name   = buffer + (pwd->pw_name   - line);
            pwd->pw_passwd = buffer + (pwd->pw_passwd - line);
            pwd->pw_gecos  = buffer + (pwd->pw_gecos  - line);
            pwd->pw_dir    = buffer + (pwd->pw_dir    - line);
            pwd->pw_shell  = buffer + (pwd->pw_shell  - line);
        }
    }
    free(line);
    fclose(passwd);
    return 0;
}