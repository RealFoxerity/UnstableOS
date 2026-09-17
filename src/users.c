#include "kernel_sched.h"
#include <sys/types.h>
#include <errno.h>

int sys_setgid(gid_t gid) {
    if (gid == -1)
        return -EINVAL;
    spinlock_acquire(&current_process->lock);
    int ret = 0;
    if (current_process->euid == 0) {
        current_process->gid = current_process->egid = current_process->sgid = gid;
    } else {
        if (gid == current_process->gid || gid == current_process->sgid)
            current_process->egid = gid;
        else
            ret = -EPERM;
    }
    spinlock_release(&current_process->lock);
    return ret;
}
int sys_setegid(gid_t gid) {
    if (gid == -1)
        return -EINVAL;
    int ret = 0;
    spinlock_acquire(&current_process->lock);
    if (current_process->euid == 0 || gid == current_process->gid || gid == current_process->sgid) {
        current_process->egid = gid;
    } else ret = -EPERM;
    spinlock_release(&current_process->lock);
    return ret;
}
int sys_setregid(gid_t rgid, gid_t egid) {
    // not sure whether this order of operation is correct
    // however it seems logical so no clue
    int ret = -EPERM;
    spinlock_acquire(&current_process->lock);
    if (rgid != -1 &&
        current_process->euid != 0 &&
        rgid != current_process->sgid)
        goto end;
    if (egid != -1 &&
        current_process->euid != 0 &&
        egid != current_process->sgid &&
        egid != current_process->gid)
        goto end;

    ret = 0;
    if (rgid != -1)
        current_process-> gid  = rgid;
    if (egid != -1)
        current_process->egid  = egid;
    if (current_process->egid != current_process-> gid || rgid != -1)
        current_process->sgid  = current_process->egid;

    end:
    spinlock_release(&current_process->lock);
    return ret;
}
int sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    // same as above, not really sure
    // also I don't really understand the rgid check?
    // POSIX says setregid checks rgid against sgid,
    //  but setresgid says:
    //      A non-privileged process can set its RGID, EGID, and SGID,
    //      each to one of the values that it currently holds in its RGID, EGID, or SGID.
    //  which means it checks rgid against rgid, egid, and sgid?

    int ret = -EPERM;
    spinlock_acquire(&current_process->lock);
    if (rgid != -1 &&
        current_process->euid != 0 &&
        rgid != current_process-> gid &&
        rgid != current_process->egid &&
        rgid != current_process->sgid)
        goto end;
    if (egid != -1 &&
        current_process->euid != 0 &&
        egid != current_process-> gid &&
        egid != current_process->egid &&
        egid != current_process->sgid)
        goto end;
    if (sgid != -1 &&
        current_process->euid != 0 &&
        sgid != current_process-> gid &&
        sgid != current_process->egid &&
        sgid != current_process->sgid)
        goto end;

    ret = 0;
    if (rgid != -1)
        current_process-> gid  = rgid;
    if (egid != -1)
        current_process->egid  = egid;
    if (sgid != -1)
        current_process->sgid  = sgid;

    end:
    spinlock_release(&current_process->lock);
    return ret;
}
int sys_setuid(uid_t uid) {
    if (uid == -1)
        return -EINVAL;
    spinlock_acquire(&current_process->lock);
    int ret = 0;
    if (current_process->euid == 0) {
        current_process->uid = current_process->euid = current_process->suid = uid;
    } else {
        if (uid == current_process->uid || uid == current_process->suid)
            current_process->euid = uid;
        else
            ret = -EPERM;
    }
    spinlock_release(&current_process->lock);
    return ret;
}
int sys_seteuid(uid_t uid) {
    if (uid == -1)
        return -EINVAL;
    int ret = 0;
    spinlock_acquire(&current_process->lock);
    if (current_process->euid == 0 || uid == current_process->uid || uid == current_process->suid) {
        current_process->euid = uid;
    } else ret = -EPERM;
    spinlock_release(&current_process->lock);
    return ret;
}
int sys_setreuid(uid_t ruid, uid_t euid) {
    int ret = -EPERM;
    spinlock_acquire(&current_process->lock);
    if (ruid != -1 &&
        current_process->euid != 0 &&
        ruid != current_process->suid &&
        ruid != current_process->euid &&
        ruid != current_process->uid)
        goto end;
    if (euid != -1 &&
        current_process->euid != 0 &&
        euid != current_process->suid &&
        euid != current_process->euid &&
        euid != current_process->uid)
        goto end;

    ret = 0;
    if (ruid != -1)
        current_process-> uid  = ruid;
    if (euid != -1)
        current_process->euid  = euid;
    if (current_process->euid != current_process-> uid || ruid != -1)
        current_process->suid  = current_process->euid;

    end:
    spinlock_release(&current_process->lock);
    return ret;
}
int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid) {
    // once again, setresuid has a more relaxed permission model than setreuid,
    // this time the ruid check was implementation defined in setreuid,
    // I changed the setreuid implementation to allow the same change as setresuid
    int ret = -EPERM;
    spinlock_acquire(&current_process->lock);
    if (ruid != -1 &&
        current_process->euid != 0 &&
        ruid != current_process->suid &&
        ruid != current_process->euid &&
        ruid != current_process->uid)
        goto end;
    if (euid != -1 &&
        current_process->euid != 0 &&
        euid != current_process->suid &&
        euid != current_process->euid &&
        euid != current_process->uid)
        goto end;
    if (suid != -1 &&
        current_process->euid != 0 &&
        suid != current_process->suid &&
        suid != current_process->euid &&
        suid != current_process->uid)
        goto end;

    ret = 0;
    if (ruid != -1)
        current_process-> uid  = ruid;
    if (euid != -1)
        current_process->euid  = euid;
    if (suid != -1)
        current_process->suid  = suid;

    end:
    spinlock_release(&current_process->lock);
    return ret;
}