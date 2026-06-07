#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "include/args.h"

struct token_vector {
    size_t token_count;
    size_t buffer_size;
    struct token * tokens;
};

static void token_add(struct token token, struct token_vector * tokens) {
    if (tokens->buffer_size == 0) {
        tokens->buffer_size = 32;
        tokens->tokens = malloc(32*sizeof(struct token));
        if (tokens->tokens == NULL) {
            perror("ysh: tokenizer: malloc()");
            exit(1);
        }
    }
    if (tokens->token_count + 1 > tokens->buffer_size) {
        tokens->tokens = realloc(tokens->tokens,(tokens->buffer_size * 2)*sizeof(struct token));
        if (tokens->tokens == NULL) {
            perror("ysh: tokenizer: realloc()");
            exit(1);
        }
        tokens->buffer_size *= 2;
    }
    tokens->tokens[tokens->token_count] = token;
    tokens->token_count++;
}

// destroys the input buffer!
// call free on returned buffer on end
struct token * tokenize_buffer(char * buffer) {
    if (strlen(buffer) == 0) return NULL;

    struct token_vector tokens = {0};

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
                if (s == buffer || isspace(*(s-1))) {
                    token_add(
                        (struct token) {
                            .type = TOKEN_STRING,
                            .string = s
                        }, &tokens);
                }
                s--;
                continue;
            case '\"':
                if (seen_literal) continue;
                seen_quotes = !seen_quotes;
                strcpy(s, s+1);
                if (s == buffer || isspace(*(s-1))) {
                    token_add(
                        (struct token) {
                            .type = TOKEN_STRING,
                            .string = s
                        }, &tokens);
                }
                s--;
                continue;
            case '\\':
                if (seen_literal) continue;
                seen_escape = 1;

                strcpy(s, s+1);
                s--;
                continue;
            default: break;
        }

        if (!seen_literal && !seen_quotes) {
            switch (*s) {
                case '>':
                    seen_whitespace = 1; // to force new match
                    *s = '\0';
                    switch (*(s+1)) {
                        case '>':
                            s++;
                            switch (*(s+1)) {
                                case '&':
                                    s++;
                                    token_add(
                                    (struct token) {
                                        .type = TOKEN_REDIR_OUT_APPEND_FD,
                                    }, &tokens);
                                    continue;
                                default:
                                    token_add(
                                    (struct token) {
                                        .type = TOKEN_REDIR_OUT_APPEND,
                                    }, &tokens);
                                    continue;
                            }
                        case '&':
                            s++;
                            token_add(
                                (struct token) {
                                    .type = TOKEN_REDIR_OUT_FD,
                                }, &tokens);
                            continue;
                        default:
                            token_add(
                                (struct token) {
                                    .type = TOKEN_REDIR_OUT,
                                }, &tokens);
                            continue;
                    }
                case '<':
                    seen_whitespace = 1;
                    *s = '\0';
                    switch (*(s+1)) {
                        case '<':
                            s++;
                            printf("Here-document (<<) not yet implemented!\n");
                            free(tokens.tokens);
                            return NULL;
                        case '&':
                            s++;
                            token_add(
                                (struct token) {
                                    .type = TOKEN_REDIR_IN_FD,
                                }, &tokens);
                            continue;
                        case '>':
                            s++;
                            token_add(
                                (struct token) {
                                    .type = TOKEN_REDIR_INOUT,
                                }, &tokens);
                            continue;
                        default:
                            token_add(
                                (struct token) {
                                    .type = TOKEN_REDIR_IN,
                                }, &tokens);
                            continue;
                    }
                case '|':
                    seen_whitespace = 1;
                    *s = '\0';
                    token_add(
                        (struct token) {
                            .type = TOKEN_PIPE,
                        }, &tokens);
                    continue;
                default: break;
            }
        }

        if (isspace(*s)) {
            if (tokens.token_count != 0) {
                tokens.tokens[tokens.token_count - 1].is_whitespaced = 1;
            }
            if (seen_whitespace == 0) {
                *s = '\0';
            }
            seen_whitespace = 1;
            continue;
        }
        if (seen_whitespace) {
            seen_whitespace = 0;
            token_add(
                (struct token) {
                    .type = TOKEN_STRING,
                    .string = s
                }, &tokens);
        }
    }

    if (seen_literal) {
        printf("Unterminated single quotes in input!\n");
        free(tokens.tokens);
        return NULL;
    }
    if (seen_quotes) {
        printf("Unterminated double quotes in input!\n");
        free(tokens.tokens);
        return NULL;
    }
    if (seen_escape) {
        printf("Unterminated escape in input!\n");
        free(tokens.tokens);
        return NULL;
    }

    token_add((struct token){.type = TOKEN_END}, &tokens);

    return tokens.tokens;
}