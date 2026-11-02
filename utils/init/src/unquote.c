#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct args {
    char ** args;
    size_t buffer_size;
    size_t arg_count;
};
static void arg_add(char * arg, struct args * args) {
    if (args->buffer_size == 0) {
        args->buffer_size = 32;
        args->args = malloc(32*sizeof(char*));
        if (args->args == NULL) {
            perror("init: unquote: malloc()");
            exit(1);
        }
    }
    if (args->arg_count + 1 > args->buffer_size) {
        args->args = realloc(args->args,(args->buffer_size * 2)*sizeof(char*));
        if (args->args == NULL) {
            perror("init: unquote: realloc()");
            exit(1);
        }
        args->buffer_size *= 2;
    }
    args->args[args->arg_count] = arg;
    args->arg_count++;
}

// destroys the input buffer!
// call free on returned buffer on end
char ** unquote_buffer(char * buffer) {
    if (strlen(buffer) == 0) return NULL;

    struct args args = {0};

    char seen_escape     = 0; // '\'
    char seen_literal    = 0; // stuff between ', whole text is considered escaped
    char seen_quotes     = 0; // stuff between ", does use escapes
    char seen_whitespace = 1;
    for (char * s = buffer; *s != '\0'; s++) {
        if (seen_escape) {
            seen_escape = 0;
            continue;
        }

        switch (*s) {
            case '\'':
                if (seen_quotes) continue;
                seen_literal = !seen_literal;
                strcpy(s, s+1);
                if (s == buffer || isspace(*(s-1)))
                    arg_add(s, &args);
                s--;
                continue;
            case '\"':
                if (seen_literal) continue;
                seen_quotes = !seen_quotes;
                strcpy(s, s+1);
                if (s == buffer || isspace(*(s-1)))
                    arg_add(s, &args);
                s--;
                continue;
            case '\\':
                if (seen_literal) continue;
                if (seen_quotes) {
                    switch (*(s+1)) {
                        case '$':
                        case '`':
                        case '\\':
                        case '\n':
                            goto ok;
                        default: break;
                    }
                    break;
                }
                ok:
                seen_escape = 1;

                strcpy(s, s+1);
                s--;
                continue;
            default: break;
        }

        if (isspace(*s)) {
            if (seen_whitespace == 0) {
                *s = '\0';
            }
            seen_whitespace = 1;
            continue;
        }
        if (seen_whitespace) {
            seen_whitespace = 0;
            arg_add(s, &args);
        }
    }

    if (seen_literal || seen_quotes || seen_escape) {
        free(args.args);
        return NULL;
    }

    arg_add(NULL, &args);
    return args.args;
}