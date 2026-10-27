#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>

#define MAX_TRIES 5
#define BONUS_TRIES_EMPTY 5 // idk about you, but I spam enter on serial lines :p
#define TIMEOUT 30   // how long to wait on a single login attempt before kicking out
#define PUNISHMENT 3 // :3 how long to time out on password failure
char hostname[HOST_NAME_MAX+1];
struct termios orig;

void alrm_handler(int sig) {
    static int timed_out_here = 0;
    if (timed_out_here == 1) // can do this because SA_NODEFER
        _exit(EXIT_SUCCESS); // _exit in case we'd hang on the tty in fflush()es
    timed_out_here = 1;
    alarm(3); // hang on tty
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    if (sig == SIGALRM)
        printf("\r\nTimed out - %d seconds elapsed\r\n", TIMEOUT);
    fflush(stdout);
    _exit(EXIT_SUCCESS);
}

void read_username(char ** username, size_t * len) {
    tcflush(STDIN_FILENO, TCIFLUSH);
    fflush(stdin);

    int c;
    int bonus_tries = BONUS_TRIES_EMPTY;
    again:
    printf("%s login: ", hostname);
    fflush(stdout);
    do {
        if ((c = getchar()) == EOF)
            exit(EXIT_FAILURE);
        if (c == '\n') {
            if (--bonus_tries == 0)
                exit(EXIT_FAILURE);
            goto again;
        }
    } while (isspace(c));
    ungetc(c, stdin);
    int read_bytes = getline(username, len, stdin);
    if (read_bytes < 0)
        exit(EXIT_FAILURE);
    if (read_bytes && (*username)[read_bytes - 1] == '\n')
        (*username)[read_bytes - 1] = '\0';
}

void read_password(char ** password, size_t * len) {
    tcflush(STDIN_FILENO, TCIFLUSH);
    fflush(stdin);
    printf("Password: ");
    fflush(stdout);
    struct termios noecho = orig;
    noecho.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ECHOCTL);
    tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
    int read_bytes = getline(password, len, stdin);
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    putchar('\n');
    if (read_bytes < 0)
        exit(EXIT_FAILURE);
    if (read_bytes && (*password)[read_bytes - 1] == '\n')
        (*password)[read_bytes - 1] = '\0';
}

char check_pw(char * password, struct passwd * user) {
    if (!user || !password ||
        !*user->pw_passwd /*|| !*password */||
        *user->pw_passwd != '$' ||
        !strchr(user->pw_passwd + 1, '$') ||
        strchr(strchr(user->pw_passwd + 1, '$') + 1, '$'))
        return 0;

    const char * salt = user->pw_passwd + 1;
    char * saltterm = strchr(salt, '$');
    *saltterm = '\0';
    const char * pw = crypt(password, salt);
    *saltterm = '$';
    if (pw && strcmp(pw, user->pw_passwd) == 0)
        return 1;
    return 0;
}

size_t gather_groups(const char * username, gid_t groups[NGROUPS_MAX]) {
    endgrent();
    struct group * group;
    size_t ngroup = 0;
    while (ngroup < NGROUPS_MAX && ((group = getgrent()))) {
        for (int i = 0; group->gr_mem[i] && ngroup < NGROUPS_MAX; i++)
            if (strcmp(group->gr_mem[i], username) == 0)
                groups[ngroup++] = group->gr_gid;
    }
    return ngroup;
}
int main(int argc, char **argv) {
    char * login_name = NULL;

    char saw_option_end = 0;
    char have_login = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && !saw_option_end) {
            if (strcmp(argv[i], "--") == 0) {
                saw_option_end = 1;
                continue;
            }
            usage:
            fprintf(stderr,
"login: Unknown option %s\n"
"Usage: login [USER]\n",argv[i]);
            return EXIT_FAILURE;
        }
        if (login_name)
            goto usage;
        login_name = strdup(argv[i]);
        have_login = 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "login: Cannot possibly work without effective root\n");
        return EXIT_FAILURE;
    }

    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &orig) < 0) {
        fprintf(stderr, "login: Input, output, or both are not a terminal device\n");
        return EXIT_FAILURE;
    }
    gid_t * groups = malloc(NGROUPS_MAX * sizeof(gid_t));
    if (!groups) {
        fprintf(stderr, "login: Failed to allocate array for supplementary groups\n");
        return EXIT_FAILURE;
    }

    if (gethostname(hostname, HOST_NAME_MAX+1) < 0)
        strcpy(hostname, "What hostname?");

    struct sigaction alrm = {
        .sa_flags = SA_NODEFER,
        .sa_handler = alrm_handler,
    };
    sigaction(SIGALRM, &alrm, NULL);
    sigaction(SIGINT, &alrm, NULL);
    int tries = MAX_TRIES;
    size_t login_size = login_name ? strlen(login_name) + 1 : 0;
    char * password = NULL;
    size_t pw_len = 0;
    struct passwd * user = NULL;
    while (--tries) {
        alarm(TIMEOUT); // fresh timeout for each attempt
        if (!have_login)
            read_username(&login_name, &login_size);
        have_login = 0;

        user = getpwnam(login_name);

        read_password(&password, &pw_len);

        struct timespec next_try;
        clock_gettime(CLOCK_MONOTONIC, &next_try);
        next_try.tv_sec += PUNISHMENT;
        alarm(0);
        if (check_pw(password, user))
            break;
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_try, NULL) == -1 && errno == EINTR) {}
    }
    signal(SIGALRM, SIG_DFL);
    signal(SIGINT, SIG_DFL);

    alarm(3); // guarding against exit's flushes and printf
    if (tries == 0 || user == NULL)
        exit(EXIT_FAILURE);

    if (!user->pw_shell) {
        fprintf(stdout, "User account locked\n");
        exit(EXIT_FAILURE);
    }
    alarm(0);

    // we want to be in the root due to security reasons
    // here in the remote possibility / is not user accessible
    chdir("/");

    size_t ngroup = gather_groups(login_name, groups);
    setgroups(ngroup, groups);
    setresgid(user->pw_gid, user->pw_gid, user->pw_gid);
    setresuid(user->pw_uid, user->pw_uid, user->pw_uid);

    const char * term = getenv("TERM");
    clearenv();
    if (term)
        setenv("TERM", term, 1);
    setenv("PATH", "/bin:/sbin", 1);
    if (user->pw_dir)
        setenv("HOME", user->pw_dir, 1);
    setenv("SHELL", user->pw_shell, 1);
    setenv("LOGNAME", user->pw_name, 1);
    setenv("USER", user->pw_name, 1);
    execl(user->pw_shell, user->pw_shell, NULL);

    alarm(3);
    fprintf(stdout, "User account locked\n");
    exit(EXIT_FAILURE);
}
