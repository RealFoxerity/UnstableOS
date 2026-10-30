#include <unistd.h>
#include <UnstableOS/halt.h>
#include <UnstableOS/syscalls.h>
#include <errno.h>

int halt(int how) {
    int ret = syscall(SYSCALL_HALT, how);
    //if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    //}
    //return 0;
}