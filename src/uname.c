#include <sys/utsname.h>
#include <string.h>
#include "kernel.h"
#include "kernel_spinlock.h"

int sys_uname(struct utsname * name) {
    strncpy(name->sysname, KERNEL_NAME, _UTS_FIELD_LEN - 1);
    rw_spinlock_acquire_read(&hostname_lock);
    strncpy(name->nodename, hostname, _UTS_FIELD_LEN - 1);
    rw_spinlock_release_read(&hostname_lock);
    strncpy(name->release, KERNEL_VERSION, _UTS_FIELD_LEN - 1);
    strncpy(name->version, __DATE__" "__TIME__, _UTS_FIELD_LEN - 1);
    strncpy(name->machine, "i486", _UTS_FIELD_LEN - 1);
    return 0;
}