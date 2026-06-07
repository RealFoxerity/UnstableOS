#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "include/args.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <limits.h>

#include <assert.h>

#define PARSER_MAX_FILE_NO (OPEN_MAX)

struct arg_vector {
    size_t count;
    size_t size;
    char ** arguments;
};
struct redirection_vector {
    size_t count;
    size_t size;
    struct redirection * redirections;
};

static void arg_add(char * arg, struct arg_vector * args) {
    if (args->size == 0) {
        args->size = 32;
        args->arguments = malloc(32*sizeof(const char *));
        if (args->arguments == NULL) {
            perror("ysh: parser: malloc()");
            exit(1);
        }
    }
    if (args->count + 1 > args->size) {
        args->arguments = realloc(args->arguments,(args->size * 2)*sizeof(const char *));
        if (args->arguments == NULL) {
            perror("ysh: parser: realloc()");
            exit(1);
        }
        args->size *= 2;
    }
    args->arguments[args->count] = arg;
    args->count++;
}

static void redir_add(const struct redirection redir, struct redirection_vector * redirections) {
    if (redirections->size == 0) {
        redirections->size = 32;
        redirections->redirections = malloc(32*sizeof(struct redirection));
        if (redirections->redirections == NULL) {
            perror("ysh: parser: malloc()");
            exit(1);
        }
    }
    if (redirections->count + 1 > redirections->size) {
        redirections->redirections = realloc(redirections->redirections,(redirections->size * 2)*sizeof(struct redirection));
        if (redirections->redirections == NULL) {
            perror("ysh: parser: realloc()");
            exit(1);
        }
        redirections->size *= 2;
    }
    redirections->redirections[redirections->count] = redir;
    redirections->count++;
}

static void clean_up_args(const struct arg_vector * args, const struct redirection_vector * redirections) {
    free(args->arguments);
    for (size_t i = 0; i < redirections->count; i++)
        if (redirections->redirections[i].redir_type == REDIR_FD_FILE)
            close(redirections->redirections[i].fd_r);
    free(redirections->redirections);
}

char * const** extract_args(struct token * tokens, struct redirection *** redir_out) {
    assert(tokens);
    assert(redir_out);

    size_t pipe_count = 1;

    size_t arg_counter = 0; // to check against stuff like | |

    // idea is to get all arguments into a single list and then put pointer to them in the ** down below
    struct redirection_vector redirs = {0};
    struct arg_vector args = {0};

    for (struct token * token = tokens; token->type != TOKEN_END; token ++) {
        struct redirection new_redir = {0};
        int open_flags = 0;

        char * match_end = NULL;
        const char * string_end = NULL;
        int fd = -1;

        switch (token->type) {
            case TOKEN_END:
            case TOKEN_EMPTY: break;

            case TOKEN_PIPE:
                if (arg_counter == 0) {
                    printf("Empty pipe encountered!\n");
                    clean_up_args(&args, &redirs);
                    return NULL;
                }
                pipe_count++;
                arg_counter = 0;
                redir_add((struct redirection) {.redir_type = REDIR_END}, &redirs);
                arg_add(NULL, &args);
                continue;
            case TOKEN_REDIR_OUT_FD:
            case TOKEN_REDIR_OUT_APPEND_FD:
                new_redir.fd_l ++; // cheeky way to get to stdin/stdout without ifs
            case TOKEN_REDIR_IN_FD:
                new_redir.redir_type = REDIR_FD_FD;
                // optional fd redirection n>&m
                // posix says it has to be directly before,
                // and whole string needs to be a number
                if (token != tokens &&
                    (token - 1)->type == TOKEN_STRING &&
                    !(token - 1)->is_whitespaced
                ) {
                    match_end  = NULL;
                    string_end = (token - 1)->string + strlen((token - 1)->string);
                    errno = 0;
                    fd = strtol((token - 1)->string, &match_end, 10);
                    if (errno == 0 && match_end == string_end && fd >= 0 && fd < PARSER_MAX_FILE_NO) {
                        new_redir.fd_l = fd;
                        args.count --; // remove the previously parsed string (the fd number)
                        arg_counter --; // same thing
                    }
                }

                if ((++token)->type != TOKEN_STRING) {
                    printf("Unexpected token on redirection r-value, expected file descriptor!\n");
                    clean_up_args(&args, &redirs);
                    return NULL;
                }

                if (strcmp(token->string, "-") == 0) {
                    new_redir.fd_r = -1;
                    redir_add(new_redir, &redirs);
                    continue;
                }

                match_end = NULL;
                string_end = token->string + strlen(token->string);
                errno = 0;
                fd = strtol(token->string, &match_end, 10);
                if (errno == 0 && match_end == string_end && fd >= 0) {
                    if (fd >= PARSER_MAX_FILE_NO) {
                        printf("Redirection file descriptor %d higher than maximum supported!\n", fd);
                        clean_up_args(&args, &redirs);
                        return NULL;
                    }
                    new_redir.fd_r = fd;
                }
                else {
                    printf("Invalid file descriptor `%s` on redirection r-value!\n", token->string);
                    clean_up_args(&args, &redirs);
                    return NULL;
                }
                redir_add(new_redir, &redirs);
                token->type = TOKEN_EMPTY; // guard against >&1> file, which would parse as >&1 1>file
                continue;

            case TOKEN_REDIR_OUT:
            case TOKEN_REDIR_OUT_APPEND:
            case TOKEN_REDIR_INOUT:
            case TOKEN_REDIR_IN:
                new_redir.redir_type = REDIR_FD_FILE;
                switch (token->type) {
                    case TOKEN_REDIR_OUT:
                        new_redir.fd_l = 1;
                        open_flags = O_CREAT | O_WRONLY | O_TRUNC;
                        break;
                    case TOKEN_REDIR_OUT_APPEND:
                        new_redir.fd_l = 1;
                        open_flags = O_CREAT | O_WRONLY | O_APPEND;
                        break;
                    case TOKEN_REDIR_INOUT:
                        new_redir.fd_l = 0;
                        open_flags = O_CREAT | O_RDWR;
                        break;
                    case TOKEN_REDIR_IN:
                        new_redir.fd_l = 0;
                        open_flags = O_RDONLY;
                        break;
                    default: break;
                }

                if (token != tokens &&
                    (token - 1)->type == TOKEN_STRING &&
                    !(token - 1)->is_whitespaced
                ) {
                    match_end  = NULL;
                    string_end = (token - 1)->string + strlen((token - 1)->string);
                    errno = 0;
                    fd = strtol((token - 1)->string, &match_end, 10);
                    if (errno == 0 && match_end == string_end && fd >= 0 && fd < PARSER_MAX_FILE_NO) {
                        new_redir.fd_l = fd;
                        args.count --; // remove the previously parsed string (the fd number)
                        arg_counter --;
                    }
                }

                if ((++token)->type != TOKEN_STRING) {
                    printf("Unexpected token on redirection r-value, expected file name!\n");
                    clean_up_args(&args, &redirs);
                    return NULL;
                }

                if ((new_redir.fd_r = open(token->string, open_flags | O_CLOEXEC, 0666)) == -1) {
                    perror("ysh: parser: warning, open()");
                } else {
                    redir_add(new_redir, &redirs);
                }
                token->type = TOKEN_EMPTY; // guard against >&1>file, which would parse as >&1 1>file
                continue;
            case TOKEN_STRING:
                arg_counter ++;
                arg_add(token->string, &args);
                break;
        }
    }
    redir_add((struct redirection) {.redir_type = REDIR_END}, &redirs);
    arg_add(NULL, &args);

    char * const** argvs = malloc(sizeof(char **) * (pipe_count + 1));

    if (argvs == NULL) {
        perror("ysh: parser: malloc()");
        return NULL;
    }

    struct redirection ** redirections = malloc(sizeof(struct redirection *) * (pipe_count + 1));
    if (redirections == NULL) {
        perror("ysh: parser: malloc()");
        return NULL;
    }

    argvs[pipe_count] = NULL;
    redirections[pipe_count] = NULL;

    size_t current_pipe_idx = 0;
    argvs[0] = args.arguments;

    for (size_t i = 0; i < args.count - 1; i++) {
        assert(current_pipe_idx < pipe_count);
        if (args.arguments[i] == NULL) {
            current_pipe_idx ++;
            argvs[current_pipe_idx] = &args.arguments[i+1];
        }
    }

    current_pipe_idx = 0;
    redirections[0] = redirs.redirections;
    for (size_t i = 0; i < redirs.count - 1; i++) {
        assert(current_pipe_idx < pipe_count);
        if (redirs.redirections[i].redir_type == REDIR_END) {
            current_pipe_idx ++;
            redirections[current_pipe_idx] = &redirs.redirections[i+1];
        }
    }
    *redir_out = redirections;
    return argvs;
}