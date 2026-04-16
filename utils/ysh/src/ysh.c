#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <sys/wait.h>

void show_help() {
    printf("\n"
                "shell builtins:\n"
                "\tcd [path] - changes current directory\n"
                "\tchroot [path] - changes the root directory for this process\n"
                "\texit [exitcode] - exits\n"
                );
}

#define MAX_INPUT_BUFFER 128
extern char ** environ;

char ** extract_args(char * args_start) {
    size_t arg_counter = 0;

    char seen_space = 1;
    char seen_escape = 0;
    char literal_section = 0;
    char seen_quote = 0;

    for (int i = 0; args_start[i] != '\0'; i++) {
        if (args_start[i] == '\\' && !seen_escape && !literal_section) {
            seen_escape = 1;
            continue;
        }
        if (seen_escape) {
            seen_escape = 0;
            goto pre_escaped;
        }
        seen_escape = 0;

        switch (args_start[i]) {
            case '\'':
                if (seen_quote) break;
                if (!literal_section) arg_counter++;
                literal_section = !literal_section;
                continue;
            case '\"':
                if (literal_section) break;
                if (!seen_quote) arg_counter++;
                seen_quote = !seen_quote;
                continue;
            default: break;
        }

        pre_escaped:
        if (literal_section || seen_quote) continue;
        if (isspace(args_start[i])) {
            seen_space = 1;
        } else {
            if (seen_space) {
                arg_counter ++;
                seen_space = 0;
            }
        }
    }

    if (arg_counter == 0) return NULL;

    if (seen_escape) {
        printf("Unterminated escape character in input!\n");
        return NULL;
    }
    if (seen_quote || literal_section) {
        printf("Unterminated quote in input!\n");
        return NULL;
    }

    char ** args = malloc((arg_counter+1) * sizeof(char *));
    if (args == NULL) return NULL;
    args[arg_counter] = NULL;

    seen_space = 1;
    arg_counter = 0;
    seen_escape = 0;
    literal_section = 0;
    seen_quote = 0;

    for (int i = 0; args_start[i] != '\0'; i++) {
        if (args_start[i] == '\\' && !seen_escape && !literal_section) {
            seen_escape = 1;
            continue;
        }
        if (seen_escape) {
            memcpy(args_start + i - 1, args_start + i, strlen(args_start + i) + 1);
            i--;
            seen_escape = 0;
            goto escaped;
        }
        seen_escape = 0;

        switch (args_start[i]) {
            case '\'':
                if (seen_quote) break;
                if (!literal_section) {
                    args[arg_counter] = args_start + i + 1;
                    arg_counter++;
                } else {
                    args_start[i] = '\0';
                }
                literal_section = !literal_section;
                continue;
            case '\"':
                if (literal_section) break;
                if (!seen_quote) {
                    args[arg_counter] = args_start + i + 1;
                    arg_counter++;
                } else {
                    args_start[i] = '\0';
                }
                seen_quote = !seen_quote;
                continue;
            default: break;
        }

        escaped:
        if (literal_section || seen_quote) continue;
        if (isspace(args_start[i])) {
            args_start[i] = '\0';
            seen_space = 1;
        } else {
            if (seen_space) {
                args[arg_counter] = args_start+i;
                arg_counter ++;
                seen_space = 0;
            }
        }
    }

    return args;
}

int main(int argc, char ** argv) {
    printf("\n\nArguments:\n");
    for (int i = 0; i < argc; i++) {
        printf("%s ", argv[i]);
    }

    printf("\nEnvironment:\n");
    for (int i = 0; environ[i] != NULL; i++) {
        printf("%s ", environ[i]);
    }
    printf("\nTesting shell env, help for help\n");
    char input_buf[MAX_INPUT_BUFFER];
    ssize_t read_bytes = 0;
    int wstatus;

    signal(SIGINT, SIG_IGN);

    while(1) {
        memset(input_buf, 0, MAX_INPUT_BUFFER);
        printf("> ");
        read_bytes = read(0, input_buf, MAX_INPUT_BUFFER - 1);
        if (read_bytes == -1 && errno == EINTR) continue; // ctrl+c input cancel
        if (read_bytes < 0) perror("read()");
        assert(read_bytes > 0);

        // strcmp here ok since we don't read into the last null byte
        if (strcmp("help\n", input_buf) == 0) {
            show_help();
        } else if (strcmp("exit ", input_buf) == 0) {
            long exitcode;
            if (sscanf(input_buf, "exit %lu", &exitcode) != 1) {
                printf("Bad argument!\n");
                continue;
            }
            exit(exitcode);
        } else if (strcmp("cd ", input_buf) == 0) {
            input_buf[read_bytes - 1] = '\0';
            char * path = input_buf + 3;
            printf("chdir: %d\n", chdir(path));
        } else if (strcmp("chroot ", input_buf) == 0) {
            input_buf[read_bytes - 1] = '\0';
            char * path = input_buf + 7;
            printf("chroot: %d\n", chroot(path));
        }  else {
            input_buf[read_bytes - 1] = '\0'; // get rid of newline
            char * prompt = NULL;
            for (int i = 0; input_buf[i] != '\0'; i++) {
                if (!isspace(input_buf[i])) {
                    prompt = input_buf + i;
                    break;
                }
            }

            if (prompt == NULL) {
                printf("? ");
                continue;
            }

            char ** new_argv = extract_args(prompt);
            if (new_argv == NULL) {
                printf("? ");
                continue;
            }

            // find pipes
            char ** process2 = NULL;
            for (int i = 0; new_argv[i] != NULL; i++) {
                if (strlen(new_argv[i]) == 1 && new_argv[i][0] == '|') {
                    if (new_argv[i + 1] == NULL || *new_argv[i + 1] == 0) {
                        printf("Unterminated pipe in input!\n");
                        goto fail;
                    }
                    new_argv[i] = NULL;
                    process2 = &new_argv[i + 1];
                }
            }

            int pipefd[2] = {-1};
            if (process2)
                assert(pipe(pipefd) == 0);
            switch (fork()) {
                case 0:
                    if (process2) {
                        switch (fork()) {
                            case 0:
                                close(pipefd[1]);
                                close(STDIN_FILENO);
                                dup2(pipefd[0], STDIN_FILENO);
                                execvp(process2[0], process2);
                                free(new_argv);
                                return 127;
                            default:
                                close(pipefd[0]);
                                close(STDOUT_FILENO);
                                dup2(pipefd[1], STDOUT_FILENO);
                                close(pipefd[1]);
                        }
                    }
                    execvp(prompt, new_argv);

                    free(new_argv);
                    return 127;
                default:
                    while (wait(&wstatus) == -1 && errno == EINTR) {}
                    if (WIFSIGNALED(wstatus)) {
                        printf("WSIG: %d ", WTERMSIG(wstatus));
                        break;
                    }
                    printf("%d ", WEXITSTATUS(wstatus));
            }
            fail:
            free(new_argv);
        }
    }
}