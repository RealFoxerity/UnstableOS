// SPDX-License-Identifier: AGPL-3.0-only OR GPL-2.0-or-later
/**
 * Echo utility
 *
 * This utility is defined by the POSIX standard and should adhere to its specification.
 * Note that this implementation includes also the XSI extension.
 *
 * See https://pubs.opengroup.org/onlinepubs/9799919799/utilities/echo.html.
 *
 * Copyright (c) 2026 Richard Tichý <richard@tichy.io>
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static const char *program_name = "echo";
static bool no_trailing_newline = false;
static bool interpret_backslash_escapes = false;

static void print_escaped_byte_val(char **cursor) {
    int base;
    if (**cursor == '0') {
        base = 8;
    } else if (**cursor == 'x') {
        base = 16;
    } else {
        return;
    }
    const long val = strtol(++*cursor, cursor, base);
    if (val < 0 || val > 255) {
        return;
    }
    fputc(val, stdout);
}

static void print_escaped_arg(char *arg) {
    bool escaping_sequence = false;
    char *cursor = arg;
    if (*cursor == '\0') {
        return;
    }

    do {
        if (escaping_sequence) {
            escaping_sequence = false;
            switch (*cursor) {
                case '\\':
                    fputc('\\', stdout);
                    break;
                case 'a':
                    fputc('\a', stdout);
                    break;
                case 'b':
                    fputc('\b', stdout);
                    break;
                case 'c':
                    exit(0);
                case 'e':
                    // escape char
                    fputc('\x1b', stdout);
                    break;
                case 'f':
                    fputc('\f', stdout);
                    break;
                case 'n':
                    fputc('\n', stdout);
                    break;
                case 'r':
                    fputc('\r', stdout);
                    break;
                case 't':
                    fputc('\t', stdout);
                    break;
                case 'v':
                    fputc('\v', stdout);
                    break;
                case '0':
                case 'x':
                    print_escaped_byte_val(&cursor);
                    cursor--;
                    break;
                default:
                    fputc('\\', stdout);
                    fputc(*cursor, stdout);
                    break;
            }
        } else if (*cursor == '\\') {
            escaping_sequence = true;
        } else {
            fputc(*cursor, stdout);
        }
    } while (*++cursor != '\0');
}

static int try_parse_opts(const char *arg) {
    if (arg[0] != '-') {
        // Flags must start with `-`
        return 1;
    }
    bool opt_no_trailing_newline = false;
    bool opt_interpret_backslash_escapes = false;
    char c;
    const char *cursor = arg;
    while ((c = *++cursor)) {
        switch (c) {
            case 'n':
                opt_no_trailing_newline = true;
                break;
            case 'e':
                opt_interpret_backslash_escapes = true;
                break;
            case 'E':
                opt_interpret_backslash_escapes = false;
                break;
            default:
                return 1;
        }
    }
    if (cursor - 1 <= arg) {
        // No flags passed
        return 1;
    }
    no_trailing_newline = opt_no_trailing_newline;
    interpret_backslash_escapes = opt_interpret_backslash_escapes;
    return 0;
}

int main(const int argc, char **argv) {
    int i = 0;
    int first_string_idx = 0;

    program_name = argv[0];

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (try_parse_opts(arg)) {
            // Failed to parse flags - this is the first string to output
            break;
        }
    }
    first_string_idx = i;

    for (i = 0; i + first_string_idx < argc; i++) {
        if (i > 0) {
            fputc(' ', stdout);
        }
        char *arg = argv[first_string_idx + i];
        if (interpret_backslash_escapes) {
            print_escaped_arg(arg);
        } else {
            fputs(arg, stdout);
        }
    }

    if (!no_trailing_newline) {
        fputc('\n', stdout);
    }

    return 0;
}
