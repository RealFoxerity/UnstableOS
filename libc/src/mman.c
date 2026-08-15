#include <unistd.h>
#include <sys/mman.h>
#include <UnstableOS/syscalls.h>
#include <sys/types.h>
#include <stddef.h>
#include <errno.h>

void *mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off) {
    void * ret = (void*)syscall(SYSCALL_MMAP, addr, len, prot, flags, fildes, &off);
    if (ret > (void*)-100) {
        ___set_errno(-(long)ret);
        return MAP_FAILED;
    }
    return ret;
}

int munmap(void *addr, size_t len) {
    int ret = syscall(SYSCALL_MUNMAP, addr, len);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return 0;
}

int mprotect(void *addr, size_t len, int prot) {
    long ret = syscall(SYSCALL_MPROTECT, addr, len, prot);
    if (ret < 0) {
        ___set_errno(-ret);
        return -1;
    }
    return ret;
}