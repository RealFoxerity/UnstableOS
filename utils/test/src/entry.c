#include "../../../libc/src/include/stdio.h"
#include "../../../libc/src/include/errno.h"
#include "../../../libc/src/include/dirent.h"
#include "../../../libc/src/include/string.h"
#include "../../../libc/src/include/signal.h"
#include "../../../libc/src/include/stdlib.h"
#include "../../../libc/src/include/unistd.h"
#include "../../../libc/src/include/fcntl.h"
#include "../../../src/include/kernel.h"
#include "../../../libc/src/include/uthreads.h"
#include "../../../libc/src/include/ctype.h"
#include "../../../libc/src/include/time.h"
#include "../../../libc/src/include/signal.h"
#include <stdint.h>

extern void malloc_print_heap_objects();

int test_thread(struct uthread_args * self, void* test_val) {
    printf("Peak testing thread :3 random magic value: %ld\n", (long)test_val);
    for (int i = 0; i < 10000000; i++) {
        for (int i = 0; i < 100; i++);
    }
    return 0;
}

void show_help() {
    printf("\n"
                "shell builtins:\n"
                "\tcd [path] - changes current directory\n"
                "\tchroot [path] - changes the root directory for this process\n"
                "\tr - print a random number\n"
                "\texit [exitcode] - exits\n"
                "raw operations:\n"
                "\tmalloc [num] - run malloc with size as integer\n"
                "\theap - print heap\n"
                "\tt - run a test thread, get return code\n");
}

#define MAX_INPUT_BUFFER 128
extern char ** environ;

char ** extract_args(char * args_start) {
    size_t arg_counter = 0;

    char seen_space = 1;
    for (int i = 0; args_start[i] != '\0'; i++) {
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

    char ** args = malloc((arg_counter+1) * sizeof(char *));
    if (args == NULL) return NULL;

    seen_space = 1;
    arg_counter = 0;
    for (int i = 0; args_start[i] != '\0'; i++) {
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
        } else if (strcmp("r\n", input_buf) == 0) {
            printf("%d\n", rand());
        } else if (strcmp("t\n", input_buf) == 0) {
            uthread_t thread = uthread_create(test_thread, (void*)(long)rand());
            printf("%d\n", uthread_join(thread));
        } else if (strncmp("malloc ", input_buf, 7) == 0) {
            char * end = NULL;
            unsigned long amount = 0;

            if(sscanf(input_buf, "malloc %lu", &amount) != 1) {
                printf("Bad argument!\n");
                continue;
            }

            printf("Allocated %lu bytes at address 0x%p\n", amount, malloc(amount));
        } else if (strcmp("heap\n", input_buf) == 0) {
            malloc_print_heap_objects();
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
            char * filename = NULL;
            for (int i = 0; input_buf[i] != '\0'; i++) {
                if (!isspace(input_buf[i])) {
                    filename = input_buf + i;
                    break;
                }
            }

            if (filename == NULL) {
                printf("? ");
                continue;
            }

            char ** new_argv = extract_args(filename);
            if (new_argv == NULL) {
                printf("? ");
                continue;
            }

            switch (fork()) {
                case 0:
                    execvp(filename, new_argv);
                    return 127;
                default:
                    if (wait(&wstatus) == -1 && errno == EINTR) {
                        printf("WINTR ");
                        break;
                    }
                    if (WIFSIGNALED(wstatus)) {
                        printf("WSIG: %d ", WTERMSIG(wstatus));
                        break;
                    }
                    printf("%d ", WEXITSTATUS(wstatus));
            }
        }
    }
}