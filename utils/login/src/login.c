#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int prompt_input_secret(char *prompt, char **buffer_out) {
    if (buffer_out == NULL) {
        return 1;
    }

    int ret = 1;

    struct termios old_flags, new_flags;
    if (tcgetattr(STDIN_FILENO, &old_flags) < 0) {
        perror("tcgetattr()");
        return 1;
    }

    new_flags = old_flags;
    new_flags.c_lflag &= ~(ECHO|ECHOK|ECHONL);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_flags) < 0) {
        perror("tcsetattr()");
        return 1;
    }

    printf("%s", prompt);
    fflush(stdout);
    char *buffer = NULL;
    size_t buffer_len = 0;
    if (getline(&buffer, &buffer_len, stdin) <= 0) {
        perror("getline()");
        goto restore_tcattr;
    }
    size_t input_len = strlen(buffer);
    if (input_len == 0 || buffer[input_len - 1] != '\n') {
        perror("getline()");
        free(buffer);
        goto restore_tcattr;
    }
    buffer[input_len - 1] = '\0';

    *buffer_out = buffer;
    ret = 0;

restore_tcattr:
    tcsetattr(STDIN_FILENO, TCSANOW, &old_flags);
    printf("\n");

    return ret;
}

static int validate_password(const char *password, const char *stored_hash) {
    // format: $salt$hash
    // salt's length is <0; 32> bytes
    // hash's length is 64 bytes
    // we need to extract just the contents between the two dollars
    if (stored_hash[0] != '$') {
        return 1;
    }
    char *sep = strchr(stored_hash + 1, '$');
    size_t salt_len = sep - stored_hash - 1;
    if (strchr(sep + 1, '$')) {
        return 1;
    }
    if (salt_len > 32) {
        return 1;
    }
    char *hash = sep + 1;
    if (strlen(hash) != 64) {
        return 1;
    }
    char salt[33]; // at most 32 + null
    memcpy(salt, stored_hash + 1, salt_len);
    salt[salt_len] = '\0';
    char *hashed_input = crypt(password, salt);
    if (hashed_input == NULL) {
        return 1;
    }
    if (strcmp(hashed_input, stored_hash) != 0) {
        return 1;
    }
    return 0;
}

static int initgroups(const char *login, gid_t base_gid) {
    gid_t groups[NGROUPS_MAX + 1];
    int ngroups = 0;

    groups[ngroups++] = base_gid;

    setgrent();
    struct group *grp;

    while ((grp = getgrent()) != NULL) {
        if (grp->gr_gid == base_gid) {
            continue;
        }

        for (int i = 0; grp->gr_mem[i] != NULL; i++) {
            if (strcmp(grp->gr_mem[i], login) == 0) {
                if (ngroups < NGROUPS_MAX) {
                    groups[ngroups++] = grp->gr_gid;
                }
                break;
            }
        }
    }
    endgrent();

    if (setgroups(ngroups, groups) != 0) {
        perror("setgroups()");
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2 || argv[1] == NULL || *argv[1] == '\0') {
        fprintf(stderr, "Usage: %s <login>\n", argv[0]);
        return 1;
    }
    const char *login = argv[1];

    char *password = NULL;
    if (prompt_input_secret("password: ", &password) > 0) {
        return 1;
    }

    const struct passwd *pwd = getpwnam(login);
    if (pwd == NULL) {
        goto login_incorrect;
    }
    const struct group *grp = getgrgid(pwd->pw_gid);
    if (grp == NULL) {
        goto login_incorrect;
    }

    if (pwd->pw_passwd == NULL) {
        goto login_incorrect;
    }

    if (validate_password(password, pwd->pw_passwd) > 0) {
        goto login_incorrect;
    }

    if (initgroups(login, pwd->pw_gid) > 0) {
        goto login_incorrect;
    }

    if (setgid(pwd->pw_gid)) {
        perror("setgid()");
        return 1;
    }

    if (setuid(pwd->pw_uid)) {
        perror("setuid()");
        return 1;
    }

    setenv("LOGNAME", pwd->pw_name, 1);
    setenv("USER", pwd->pw_name, 1);
    setenv("HOME", pwd->pw_dir, 1);
    setenv("SHELL", pwd->pw_shell, 1);
    setenv("PATH", "/bin:/sbin", 1);

    execv(pwd->pw_shell, NULL);
    perror("execv()");
    return 1;

login_incorrect:
    fprintf(stderr, "Login incorrect\n");

    return 1;
}
