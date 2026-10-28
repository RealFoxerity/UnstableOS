#include <unistd.h>
#include <sys/utsname.h>
#include <errno.h>
#include <UnstableOS/syscalls.h>

int uname(struct utsname *name) {
    int ret = syscall(SYSCALL_UNAME, name);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return 0;
}
