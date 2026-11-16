#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define TOOLONG { \
		___set_errno(ENAMETOOLONG); \
		return NULL; \
	}

char * realpath(const char * restrict file_name, char * restrict resolved_name) {
    if (!file_name) {
        ___set_errno(EFAULT);
        return NULL;
    }
    if (!*file_name) {
        ___set_errno(ENOENT);
        return NULL;
    }

    char path[PATH_MAX + 1];
    char output[PATH_MAX];
    size_t l0, nup      = 0;
    char was_dir        = 0; // as opposed to it being a symlink we're resolving
    unsigned long depth = 0;

    size_t l = strnlen(file_name, PATH_MAX + 1);
    if (l >= PATH_MAX) {
        ___set_errno(ENAMETOOLONG);
        return NULL;
    }
    size_t path_off   = PATH_MAX - l;
    size_t output_len = 0;
    memcpy(path + path_off, file_name, l + 1);

restart:
    for (; ; path_off += strspn(path + path_off, "/")) {
        // absolute path
        if (path[path_off] == '/') {
            was_dir    = 0;
            nup        = 0;
            output[0]  = '/';
            output_len = 1;
            path_off++;
            // our meta directory; // is implementation defined so preserve it
            if (path[path_off] == '/' && path[path_off + 1] != '/')
                output[output_len++] = '/';
            continue;
        }

        char * next = strchrnul(path + path_off, '/');
        l0          = l = next - (path + path_off);

        if (!l && !was_dir)
            break;

        if (l == 1 && path[path_off] == '.') {
            path_off++;
            continue;
        }

        // add / if output doesn't have it already
        if (output_len && output[output_len - 1] != '/') {
            if (!path_off) TOOLONG;
            path[--path_off] = '/';
            l++;
        }

        if (output_len + l >= PATH_MAX) TOOLONG;

        // copy component and null terminate it so we can pass it to readlink
        memcpy(output + output_len, path + path_off, l);
        output[output_len + l] = 0;
        path_off               += l;

        int go_up = 0;
        if (l0 == 2 && !strncmp(path + path_off - 2, "..", 2)) {
            go_up = 1;
            // is there even anything to remove?
            // if not (the entire string is ../../../), increase the counter to remove later
            if (output_len <= 3 * nup) {
                nup++;
                output_len += l;
                continue;
            }

            if (!was_dir)
                goto skip_readlink;
        }
        ssize_t link_len = readlink(output, path, path_off);
        if (!link_len) {
            ___set_errno(ENOENT);
            return 0;
        }
        if (link_len == path_off) TOOLONG;
        if (link_len < 0) {
            if (___get_errno() != EINVAL)
                return NULL;
        skip_readlink:
            was_dir = 0;
            if (go_up) {
                while (output_len && output[output_len - 1] != '/')
                    output_len--;
                // preserve '//'
                if (output_len > 1 && (output_len > 2 || output[0] != '/'))
                    output_len--;
                continue;
            }
            if (l0)
                output_len += l;
            was_dir = !!path[path_off];
            continue;
        }
        if (++depth == SYMLOOP_MAX) {
            ___set_errno(ELOOP);
            return NULL;
        }

        // remove any extra trailing newlines from the symlink
        if (path[link_len - 1] == '/')
            while (path[path_off] == '/')
                path_off++;
        path_off -= link_len;
        memmove(path + path_off, path, link_len);

        // skip the for loop's next statement if this is a start of a new absolute path
        goto restart;
    }

    output[output_len] = 0;

    if (output[0] != '/') {
        if (!getcwd(path, PATH_MAX + 1))
            return NULL;
        l = strlen(path);

        size_t off = 0;
        while (nup--) {
            while (l > 1 && path[l - 1] != '/')
                l--;
            if (l > 1)
                l--;
            off += 2;
            if (off < output_len)
                off++;
        }
        if (output_len - off && path[l - 1] != '/')
            path[l++] = '/';
        if (l + (output_len - off) + 1 >= PATH_MAX) TOOLONG;
        memmove(output + l, output + off, output_len - off + 1);
        memcpy(output, path, l);
        output_len = l + output_len - off;
    }

    if (!resolved_name)
        return strdup(output);
    return memcpy(resolved_name, output, output_len + 1);
}
