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
#include <fcntl.h>

#include "include/args.h"

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

int main(int argc, char ** argv) {
    printf("\n\nArguments:\n");
    for (int i = 0; i < argc; i++) {
        printf("%s ", argv[i]);
    }

    printf("\nEnvironment:\n");
    for (int i = 0; environ[i] != NULL; i++) {
        printf("%s ", environ[i]);
    }
    printf("\n\n");
    char input_buf[MAX_INPUT_BUFFER];
    ssize_t read_bytes = 0;

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    while(1) {
        memset(input_buf, 0, MAX_INPUT_BUFFER);
        printf("> ");
        read_bytes = read(0, input_buf, MAX_INPUT_BUFFER - 1);
        if (read_bytes == -1 && errno == EINTR) continue; // ctrl+c input cancel
        if (read_bytes < 0) {
            perror("read()");
            exit(1);
        }
        if (read_bytes == 0) return 0;

        input_buf[read_bytes - 1] = '\0'; // get rid of newline and allow for string operations


        // strcmp here ok since we don't read into the last null byte
        if (strcmp("help", input_buf) == 0) {
            show_help();
        } else if (strcmp("sync", input_buf) == 0) {
            sync();
        } else if (strcmp("exit ", input_buf) == 0) {
            long exitcode;
            if (sscanf(input_buf, "exit %lu", &exitcode) != 1) {
                printf("Bad argument!\n");
                continue;
            }
            exit(exitcode);
        } else if (strncmp("cd", input_buf, 2) == 0) {
            char * path = NULL;
            if (read_bytes == 3) {
                path = getenv("HOME");
                if (path == NULL)
                    path = "/";
            } else
                path = input_buf + 3;

            int ret = chdir(path);
            if (ret < 0)
                perror("cd");
        } else if (strncmp("chroot ", input_buf, 7) == 0) {
            char * path = input_buf + 7;
            printf("chroot: %d\n", chroot(path));
        } else {
            struct token * tokens = tokenize_buffer(input_buf);
            if (tokens == NULL) continue;

            struct redirection ** redirs = NULL;
            char * const ** argvs = extract_args(tokens, &redirs);
            free(tokens);
            if (argvs == NULL) continue;

            pid_t last_pid = 0; // return codes are based on the last process in a given pipeline
            pid_t read_pid = 0;
            int wstatus = 0;
            int last_wstatus = -1;

            int last_read_fd = -1; // what fd to dup2 to STDIN_FILENO in a forked process

            // used CLOEXEC both here and in the parser to avoid needing to close each descriptor in the fork branch
            // (just in case to not leak extra descriptors)
            for (size_t arg_idx = 0; argvs[arg_idx] != NULL; arg_idx++) {
                int pipe_fds[2] = {-1, -1};
                if (argvs[arg_idx + 1] != NULL) {
                    if (pipe2(pipe_fds, O_CLOEXEC) == -1) {
                        printf("ysh: error on argv[0]: %s: pipe2(): %s\n", argvs[arg_idx][0], strerror(errno));
                        goto command_cleanup;
                    }
                }

                switch (read_pid = fork()) {
                    case -1:
                        printf("ysh: error on argv[0]: %s: fork(): %s\n", argvs[arg_idx][0], strerror(errno));
                        close(pipe_fds[0]);
                        close(pipe_fds[1]);
                        goto command_cleanup;

                    case 0:
                        if (last_read_fd != -1)
                            dup2(last_read_fd, STDIN_FILENO);

                        for (struct redirection * redirect = redirs[arg_idx]; redirect->redir_type != REDIR_END; redirect++)
                            dup2(redirect->fd_r, redirect->fd_l);

                        if (pipe_fds[1] != -1)
                            dup2(pipe_fds[1], STDOUT_FILENO);

                        execvp(argvs[arg_idx][0], argvs[arg_idx]);
                        printf("ysh: %s: %s\n", argvs[arg_idx][0], strerror(errno));
                        exit(127);

                    default:
                        if (argvs[arg_idx + 1] == NULL)
                            last_pid = read_pid;
                        close(pipe_fds[1]);
                        close(last_read_fd);
                        last_read_fd = pipe_fds[0];

                        // close now to have as much free FDs for pipes
                        for (struct redirection * redirect = redirs[arg_idx]; redirect->redir_type != REDIR_END; redirect++) {
                            if (redirect->redir_type == REDIR_FD_FILE) {
                                close(redirect->fd_r);
                                redirect->fd_r = -1;
                            }
                        }
                }
            }

            command_cleanup:
            free((void*)argvs[0]);
            free(argvs);

            for (struct redirection ** redirects = redirs; *redirects != NULL; redirects++) {
                for (struct redirection * redirect = *redirects; redirect->redir_type != REDIR_END; redirect++) {
                    if (redirect->redir_type == REDIR_FD_FILE) {
                        close(redirect->fd_r);
                    }
                }
            }

            free(redirs[0]);
            free(redirs);

            while (1) {
                read_pid = wait(&wstatus);
                if (read_pid == -1 && errno == ECHILD) break;
                if (read_pid == last_pid) last_wstatus = wstatus;
            }
            continue;
        }
    }
}