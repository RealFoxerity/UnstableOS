#include <UnstableOS/mount.h>
#include <UnstableOS/syscalls.h>
#include <unistd.h>
#include <errno.h>

int mount(const char * dev_path, const char * mountpoint, unsigned char type, unsigned short options) {
    if (mountpoint == NULL) {
        ___set_errno(EINVAL);
        return -1;
    }
    long ret = syscall(SYSCALL_MOUNT, dev_path, mountpoint, type, options);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}
int umount(const char * mountpoint) {
    if (mountpoint == NULL) {
        ___set_errno(EINVAL);
        return -1;
    }
    long ret = syscall(SYSCALL_UMOUNT, mountpoint);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}