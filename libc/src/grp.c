#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <grp.h>
#include <errno.h>

static int __getgrent_r(FILE * f, struct group * gr, char ** line, size_t * n, char *** gr_mem, size_t * nmem, size_t * usednmem, struct group ** result) {
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
        gr->gr_name = s;

        if (!((s = strchr(s, ':')))) continue;
        *s++ = '\0';
        gr->gr_passwd = s;

        if (!((s = strchr(s, ':')))) continue;
        *s++ = '\0';
        gr->gr_gid = strtoll(s, &s, 10);

        if (*s != ':') continue;
        *s++ = '\0';
        size_t entries = 2; // 1 for the last element that doesn't have a trailing comma, 1 for the null termination
        char * mem = s;
        while (((s = strchr(s, ','))) && ++s && ++entries) {}

        char ** new = *gr_mem;
        if (!*gr_mem || entries > *nmem) {
            new = realloc(*gr_mem, entries * sizeof(char*));
            if (!new) {
                int err = ___get_errno();
                ___set_errno(old_errno);
                return -err;
            }
            *gr_mem = new;
            *nmem = entries;
        }

        if (usednmem)
            *usednmem = entries;

        new[entries - 1] = NULL;
        for (size_t i = 0; i < entries - 1; i++, mem++) {
            new[i] = mem;
            mem = strchr(mem, '\0');
            if (!mem) {
                new[i+1] = NULL; // huh?
                break;
            }
            *mem = '\0';
        }
        gr->gr_mem = new;
        break;
    }
    *result = gr;
    return read;
}

static FILE * f = NULL;

static char *  grbuf = NULL;
static char ** grmembuf = NULL;
static size_t grbuflen = 0;
static size_t grmembuflen = 0;
static struct group gr;

void endgrent() {
    if (f) fclose(f);
    f = NULL;
}
struct group *getgrent() {
    if (!f) {
        f = fopen("/etc/group", "r");
        if (!f)
            return NULL;
    }
    struct group *res;
    int ret = __getgrent_r(f, &gr, &grbuf, &grbuflen, &grmembuf, &grmembuflen, NULL, &res);
    if (ret < 0)
        ___set_errno(-ret);

    return res;
}
void setgrent() {
    if (f) rewind(f);
}
struct group *getgrnam(const char *name) {
    FILE * group = fopen("/etc/group", "r");
    if (!group)
        return NULL;
    struct group *res = NULL;
    while (1) {
        int ret = __getgrent_r(group, &gr, &grbuf, &grbuflen, &grmembuf, &grmembuflen, NULL, &res);
        if (ret < 0)
            ___set_errno(-ret);
        if (ret <= 0 || !res)
            break;
        if (gr.gr_name && strcmp(name, gr.gr_name) == 0)
            break;
    }
    fclose(group);
    return res;
}
int getgrnam_r(const char *name, struct group *grp, char *buffer, size_t bufsize, struct group **result) {
    char * line = NULL;
    size_t n = 0;
    char ** grmem = NULL;
    size_t nmem = 0;
    size_t nmem_final = 0;

    int old_errno = ___get_errno();
    FILE * group = fopen("/etc/group", "r");
    if (!group) {
        int err = ___get_errno();
        ___set_errno(old_errno);
        return err;
    }

    while (1) {
        int read = __getgrent_r(group, grp, &line, &n, &grmem, &nmem, &nmem_final, result);
        if (read < 0) {
            free(line);
            free(grmem);
            fclose(group);
            return -read;
        }
        if (!*result)
            break;
        if (read + nmem_final * sizeof(char *) > bufsize) {
            free(line);
            free(grmem);
            fclose(group);
            return ERANGE;
        }
        if (grp->gr_name && strcmp(name, grp->gr_name) == 0) {
            memcpy(buffer, line, read);
            memcpy(buffer + read, grmem, nmem_final * sizeof(char *));
            grp->gr_name   = buffer + (grp->gr_name   - line);
            grp->gr_passwd = buffer + (grp->gr_passwd - line);
            grp->gr_mem    = (void*)buffer + read;
            for (size_t i = 0; i < nmem_final; i++) {
                if (!grp->gr_mem[i])
                    continue;
                grp->gr_mem[i] = buffer + (grp->gr_mem[i] - line);
            }
            break;
        }
    }
    free(line);
    free(grmem);
    fclose(group);
    return 0;
}
struct group *getgrgid(gid_t gid) {
    FILE * group = fopen("/etc/group", "r");
    if (!group)
        return NULL;
    struct group *res;
    while (1) {
        int ret = __getgrent_r(group, &gr, &grbuf, &grbuflen, &grmembuf, &grmembuflen, NULL, &res);
        if (ret < 0)
            ___set_errno(-ret);
        if (ret <= 0 || !res)
            break;
        if (gr.gr_gid == gid)
            break;
    }
    fclose(group);
    return res;
}
int getgrgid_r(gid_t gid, struct group *grp, char *buffer, size_t bufsize, struct group **result) {
    char * line = NULL;
    size_t n = 0;
    char ** grmem = NULL;
    size_t nmem = 0;
    size_t nmem_final = 0;

    int old_errno = ___get_errno();
    FILE * group = fopen("/etc/group", "r");
    if (!group) {
        int err = ___get_errno();
        ___set_errno(old_errno);
        return err;
    }

    while (1) {
        int read = __getgrent_r(group, grp, &line, &n, &grmem, &nmem, &nmem_final, result);
        if (read < 0) {
            free(line);
            free(grmem);
            fclose(group);
            return -read;
        }
        if (!*result)
            break;
        if (read + nmem_final * sizeof(char *) > bufsize) {
            free(line);
            free(grmem);
            fclose(group);
            return ERANGE;
        }
        if (grp->gr_gid == gid) {
            memcpy(buffer, line, read);
            memcpy(buffer + read, grmem, nmem_final * sizeof(char *));
            grp->gr_name   = buffer + (grp->gr_name   - line);
            grp->gr_passwd = buffer + (grp->gr_passwd - line);
            grp->gr_mem    = (void*)buffer + read;
            for (size_t i = 0; i < nmem_final; i++) {
                if (!grp->gr_mem[i])
                    continue;
                grp->gr_mem[i] = buffer + (grp->gr_mem[i] - line);
            }
            break;
        }
    }
    free(line);
    free(grmem);
    fclose(group);
    return 0;
}