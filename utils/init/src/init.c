#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <UnstableOS/mount.h>
#include <errno.h>
#include <assert.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <UnstableOS/halt.h>

// TODO: implement SIGHUP = reload inittab



FILE * fstab, * inittab;

extern char ** unquote_buffer(char * buffer);

void handle_fstab_entry(char * line) {
    static int linenum = 0;
    linenum++;
    for (size_t i = 0; line[i]; i++) {
        if (line[i] == '#' || line[i] == '\n') {
            line[i] = '\0';
            break;
        }
    }
    if (!*line)
        return;

    int narg = 0;
    char * args[4];

    char saw_whitespace = 1;
    size_t i = 0;
    for (; line[i]; i++) {
        if (saw_whitespace && !isspace(line[i])) {
            if (narg == 4)
                break;
            saw_whitespace = 0;
            args[narg++] = line + i;
            continue;
        }
        if (!saw_whitespace && isspace(line[i])) {
            if (line[i - 1] == '\\') {
                strcpy(line + i - 1, line + i);
                i--;
                if (i < 1 || line[i - 1] != '\\') continue;
            }
            line[i] = '\0';
            saw_whitespace = 1;
        }
    }
    if (narg == 4) {
        for (; line[i]; i++) {
            if (!isspace(line[i])) {
                fprintf(stderr, "[Warn]\tfstab: Ignoring trailing data on line %d\n", linenum);
                break;
            }
            line[i] = '\0';
        }
    }
    if (narg < 3) {
        fprintf(stderr, "[Warn]\tfstab: Missing arguments on line %d\n", linenum);
        return;
    }
    int flag = 0;
    if (!strcmp(args[3], "ro"))
        flag = MOUNT_RDONLY;
    else if (strcmp(args[3], "rw"))
        fprintf(stderr, "[Warn]\tfstab: Unrecognized option on line %d, assuming rw\n", linenum);

    int ret = mount(args[0], args[1], args[2], flag);
    if (ret < 0) {
        fprintf(stderr, "[Warn]\tfstab: Failed to mount '%s': %s\n", args[1],
            errno == ENODEV ?
            "Filesystem type not supported" :
            strerror(errno));
    } else
        fprintf(stderr, "[OK  ]\tfstab: Mounted '%s'\n", args[1]);
}

void mount_fstab() {
    if (!fstab)
        return;
    char * line = NULL;
    size_t len = 0;
    while (getline(&line, &len, fstab) > 0) {
        handle_fstab_entry(line);
    }
    free(line);
}

enum action {
    ACTION_ONCE,
    ACTION_RESPAWN,
    ACTION_WAIT,
    ACTION_ASKFIRST,
    ACTION_BOOT,
    ACTION_BOOTWAIT
};

struct task {
    const char * name;
    enum action act;
    char ** argv;

    pid_t pid;

    struct task * next;
};

struct task * tasks = NULL;

void parse_inittab() {
    char * line = NULL;
    size_t len = 0;
    rewind(inittab);
    size_t lineno = 0;
    while (getline(&line, &len, inittab) > 0) {
        lineno++;

        for (size_t i = 0; line[i]; i++) {
            if (line[i] == '#' || line[i] == '\n') {
                if (i == 0 || line[i-1] != '\\' || line[i] == '\n') {
                    line[i] = '\0';
                    break;
                }
                strcpy(line + i - 1, line + i);
                i--;
            }
        }
        if (!*line)
            continue;
        for (size_t i = 0; line[i]; i++) {
            if (!isspace(line[i]))
                goto nonempty;
        }
        continue;

        nonempty:
        char * args[4];
        args[0] = line;
        for (int i = 1; i < 4; i++) {
            args[i] = strchr(args[i-1], ':');
            if (!args[i]) {
                missing_arg:
                fprintf(stderr, "[Warn]\tinittab: Missing arguments on line %zu\n", lineno);
                goto next;
            }
            *args[i] = '\0';
            args[i]++;
        }

        if (!*args[3])
            goto missing_arg;

        struct task new_task = {.name = args[0]};
        if (!strcmp(args[2], "once"))
            new_task.act = ACTION_ONCE;
        else if (!strcmp(args[2], "respawn"))
            new_task.act = ACTION_RESPAWN;
        else if (!strcmp(args[2], "wait"))
            new_task.act = ACTION_WAIT;
        else if (!strcmp(args[2], "askfirst"))
            new_task.act = ACTION_ASKFIRST;
        else if (!strcmp(args[2], "boot"))
            new_task.act = ACTION_BOOT;
        else if (!strcmp(args[2], "bootwait"))
            new_task.act = ACTION_BOOTWAIT;
        else {
            fprintf(stderr, "[Warn]\tinittab: Invalid action '%s' on line %zu\n", args[2], lineno);
            continue;
        }

        if (new_task.act == ACTION_ASKFIRST && !new_task.name[0]) {
            fprintf(stderr, "[Warn]\tinittab: 'askfirst' requires ctty in the id field on line %zu\n", lineno);
            continue;
        }

        new_task.name = strdup(new_task.name);
        assert(new_task.name);

        char * _argv = strdup(args[3]);
        assert(_argv);
        char ** argv = unquote_buffer(_argv);
        if (!argv) {
            free((void*)new_task.name);
            free(_argv);
            continue;
        }
        new_task.argv = argv;
        struct task * n = malloc(sizeof(struct task));
        assert(n);
        *n = new_task;
        n->next = tasks;
        tasks = n;

        next:
        continue;
    }
    free(line);
}

void print_task(int wstatus, struct task * t) {
    if (!wstatus) // exit 0
        return;
    fprintf(stderr,
        "[%s]\t'%s' (%d) - %s %d\n",
        WIFEXITED(wstatus) ?
            WEXITSTATUS(wstatus) == 0 ?
                "OK  " : "Fail" :
            "Fail",
        t->name,
        t->pid,
        WIFEXITED(wstatus) ?
            "Exited with ":
            "Was killed by signal ",
        WIFEXITED(wstatus) ?
            WEXITSTATUS(wstatus):
            WTERMSIG(wstatus));
}

#define CTRL(x) ((x) & 0x1F)
struct termios def = {
    .c_iflag = ICRNL | IXANY | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | ECHOCTL,
    .c_cflag = CREAD | HUPCL | CS8,

    .c_cc = {
        [VEOF]   = CTRL('D'),
        [VEOL]   = '\x00',
        [VERASE] = '\x7f',
        [VINTR]  = CTRL('C'),
        [VKILL]  = CTRL('U'),
        [VMIN]   = 1,
        [VQUIT]  = CTRL('\\'),
        [VSTART] = CTRL('Q'),
        [VSTOP]  = CTRL('S'),
        [VSUSP]  = CTRL('Z'),
        [VTIME]  = 0,
    }
};
void do_askfirst(struct task * t) {
    struct termios orig;
    if (tcgetattr(0, &orig) < 0)
        exit(EXIT_FAILURE);
    cfsetospeed(&def, cfgetospeed(&orig));
    tcsetattr(0, TCSANOW, &orig);
    fpurge(stdout);
    fflush(stdin);
    printf("Press Enter to activate this console...");
    fflush(stdout);
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {}

    execvp(t->argv[0], t->argv);
    _exit(127);
}

void launch_task(struct task * t, int was_reloaded, int respawning) {
    switch (t->act) {
        case ACTION_BOOT:
            if (was_reloaded)
                break;
        case ACTION_ONCE:
            if (respawning)
                return;
        case ACTION_RESPAWN:
            fprintf(stderr, "[Spwn]\t'%s'\n", t->name);
            if (!((t->pid = fork()))) {
                setsid();
                for (int i = 1; i <= NSIG_MAX; i++)
                    signal(i, SIG_DFL);
                execvp(t->argv[0], t->argv);
                _exit(127);
            }
            break;
        case ACTION_BOOTWAIT:
            if (was_reloaded)
                break;
        case ACTION_WAIT:
            if (respawning)
                return;
            if (!((t->pid = fork()))) {
                setsid();
                for (int i = 1; i <= NSIG_MAX; i++)
                    signal(i, SIG_DFL);
                execvp(t->argv[0], t->argv);
                _exit(127);
            }
            int wstatus;
            while (waitpid(t->pid, &wstatus, 0) != t->pid && errno == EINTR) {}
            print_task(wstatus, t);
            break;
        case ACTION_ASKFIRST: {
            char path[PATH_MAX];
            strcpy(path, "/dev/");
            strcpy(path + 5, t->name);
            if (access(path, R_OK | W_OK) < 0) {
                fprintf(stderr, "[Fail]\t'askfirst' task '%s': '%s'\n", t->name, strerror(errno));
                break;
            }
            fprintf(stderr, "[Spwn]\t'%s'\n", t->name);
            if (!((t->pid = fork()))) {
                close(0);
                close(1);
                close(2);
                setsid();
                if (open(path, O_RDWR) < 0)
                    exit(EXIT_FAILURE);
                dup2(0, 1);
                dup2(0, 2);
                for (int i = 1; i <= NSIG_MAX; i++)
                    signal(i, SIG_DFL);
                do_askfirst(t);
            }
            break;
        }

        default:
            break;
    }

}

void run_inittab(int was_reloaded) {
    struct task * t = tasks;
    while (t) {
        launch_task(t, was_reloaded, 0);
        t = t->next;
    }
}

// SIGUSR1 = halt
// SIGUSR2 = reset
// RTMIN   = force halt
// RTMIN+1 = force reset
void halt_handler(int sig) {
    switch (sig) {
        case SIGUSR1:
            halt(0);
            break;
        case SIGUSR2:
            halt(HALT_RESET);
            break;
        case SIGRTMIN:
            halt(HALT_FORCE);
            break;
        case SIGRTMIN+1:
            halt(HALT_RESET | HALT_FORCE);
            break;
        default:
            return;
    }
    fprintf(stderr, "[Warn]\tHalt failed?\n");
}
int main(int argc, char ** argv) {
    if (getpid() != 1) {
        fprintf(stderr, "Must be ran as PID 1!\n");
        return EXIT_FAILURE;
    }
    for (int i = 1; i <= NSIG_MAX; i++)
        signal(i, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);

    signal(SIGUSR1,    halt_handler);
    signal(SIGUSR2,    halt_handler);
    signal(SIGRTMIN,   halt_handler);
    signal(SIGRTMIN+1, halt_handler);

    setsid();
    ioctl(0, TIOCNOTTY);
    fcntl(0, F_SETFD, FD_CLOEXEC);
    fcntl(1, F_SETFD, FD_CLOEXEC);
    fcntl(2, F_SETFD, FD_CLOEXEC);

    fstab = fopen("/etc/fstab", "re");
    if (fstab == NULL)
        fprintf(stderr, "[Warn]\tCan't open /etc/fstab\n");
    inittab = fopen("/etc/inittab", "re");
    if (inittab == NULL) {
        fprintf(stderr, "[Crit]\tCan't open /etc/inittab\n");
        exit(EXIT_FAILURE);
    }

    mount_fstab();
    parse_inittab();
    if (!tasks) {
        fprintf(stderr, "[Crit]\tNo tasks in inittab\n");
        exit(EXIT_FAILURE);
    }
    run_inittab(0);
    while (1) {
        int wstatus;
        pid_t pid;
        if ((pid = wait(&wstatus)) > 0) {
            struct task * t = tasks;
            while (t) {
                if (t->pid == pid) {
                    print_task(wstatus, t);
                    launch_task(t, 0, 1);
                    break;
                }
                t = t->next;
            }
        }
        if (pid == -1 && errno == ECHILD)
            sleep(1);
    }
}