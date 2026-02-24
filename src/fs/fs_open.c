#include "../include/fs/fs.h"
#include "../include/fs/vfs.h"
#include "../include/kernel_sched.h"
#include "../include/errno.h"
#include "../include/mm/kernel_memory.h"

#include "../../libc/src/include/string.h"
#include "../include/kernel_tty_io.h"
#include "../include/block/memdisk.h"

int open_raw_device(dev_t device, unsigned short mode) {
    if (mode == 0 || mode > O_RDWR) return EINVAL;

    spinlock_acquire(&kernel_fd_lock);
    
    int fd = -1;
    for (int i = 0; i < FD_LIMIT_PROCESS; i++) {
        if (!current_process->fds[i]) {
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        spinlock_release(&kernel_fd_lock);
        return EMFILE;
    }

    file_descriptor_t * file = get_free_fd();
    if (!file) {
        spinlock_release(&kernel_fd_lock);
        return ENFILE;
    }

    inode_t * dev_inode = inode_from_device(device);
    kassert(dev_inode);

    file->inode = dev_inode;
    file->mode = mode;

    current_process->fds[fd] = file;

    spinlock_release(&kernel_fd_lock);

    return fd;
}

static char check_page(const char * addr) {
    if ((PAGE_DIRECTORY_TYPE*)addr >= PTE_ADDR_VIRT_BASE) return 0; // colliding with page tables
    if (addr < (const char *)LOWEST_PHYS_ADDR_ALLOWABLE) return 0; // we don't store data in low memory

    PAGE_TABLE_TYPE pte = paging_get_pte(addr);
    if (pte == 0) return 0; // not present
    if (current_process->pid != 0 && !(pte & PTE_PDE_PAGE_USER_ACCESS)) return 0; // userspace doesn't have access

    return 1;
}

static inline char * secure_strdup(const char * path, size_t max_len, size_t *len_out) {
    if (path == NULL) return NULL;

    size_t path_len;
    for (path_len = 0; path_len < max_len; path_len++) {
        if (!check_page(path + path_len)) return NULL;
        if (path[path_len] == '\0') break;
    }
    if (path_len == 0) return NULL;

    char * duped = kalloc(path_len+1); // +1 for the null byte we need to copy
    kassert(duped);

    memcpy(duped, path, path_len+1);
    *len_out = path_len;
    return duped;
}

#define MOUNT_STACK_LEN 128 // 128 mountpoint depth before giving up

int sys_open(const char * path, unsigned short mode) {
    kassert(root_mountpoint);
    int ret = -1;

    size_t pathlen;
    char * dup_path = secure_strdup(path, PATH_MAX, &pathlen);
    if (dup_path == NULL) return EFAULT;

    // deciding whether normal path (/,///+) or meta directory //
    if (dup_path[0] != '/') {
        kprintf("Stub: we don't yet support relative paths!\n");
        return EINVAL;
    } else if (
        pathlen >= sizeof(PATH_METADIR) &&
        strcmp(dup_path, PATH_METADIR) == 0 &&
        dup_path[sizeof(PATH_METADIR) - 1] != '/'
    ) {
        kprintf("Stub: we don't yet support the // meta directory!\n");
        return EINVAL;
    }
    
    // path cleanup
    char seen_slash = 0;
    for (size_t i = 0; i < pathlen; i++) {
        if (dup_path[i] == '/') {
            if (seen_slash) {
                // move everything left by one to remove second slash
                memmove(dup_path + i, dup_path + i + 1, pathlen - i - 1);
                pathlen --;
                i--; // to counteract the i++
                dup_path[pathlen] = '\0';
            } else seen_slash = 1;
        } else seen_slash = 0;
    }
    // cleanup of trailing slashes
    for (size_t i = pathlen - 1; i >= 1; i--) { // >= 1 to not remove the first /
        if (dup_path[i] == '/') {
            dup_path[i] = '\0';
            pathlen--;
        } else break;
    }


    // resolving the mount point
    spinlock_acquire(&mount_tree_lock);

    char * final_path = dup_path+1; // skip the first slash

    superblock_t * sb = root_mountpoint->mountpoint->backing_superblock;
    kassert(sb->funcs);
    kassert(sb->funcs->lookup);

    inode_t * prev = NULL, * new = NULL;

    superblock_t * sb_stack[MOUNT_STACK_LEN] = {NULL};
    inode_t * last_stack[MOUNT_STACK_LEN] = {NULL};
    size_t sb_stack_tail = 0;
    sb_stack[0] = sb;

    char last_fragment = 0;
    while (!last_fragment) {
        char * next_slash = strchrnul(final_path, '/');
        if (*next_slash == '\0') last_fragment = 1;
        *next_slash = '\0';
        
        new = sb->funcs->lookup(sb, prev, final_path);

        if (new == VFS_LOOKUP_NOTFOUND) {
            if (prev != NULL) close_inode(prev);
            ret = ENOENT;
            goto err;
        }
        if (new == VFS_LOOKUP_ESCAPE) {
            if (prev != NULL) close_inode(prev);
            final_path = next_slash + 1;
            if (sb_stack_tail == 0) continue; // nowhere to "escape" to
            sb_stack_tail --;
            prev = last_stack[sb_stack_tail];
            sb = sb_stack[sb_stack_tail];
            continue;
        }
        if (new == VFS_LOOKUP_NOTDIRECTORY) {
            if (prev != NULL) close_inode(prev);
            ret = ENOTDIR;
            goto err;
        }
        if (new->is_mountpoint) {
            if (sb_stack_tail + 1 >= MOUNT_STACK_LEN) {
                kprintf("Reached mountpoint depth of %u while resolving %s, giving up and returning ENOENT\n", sb_stack_tail, dup_path);
                if (prev != NULL) close_inode(prev);
                close_inode(new);
                ret = ENOENT;
                goto err;
            }
            sb_stack_tail ++;
            sb_stack[sb_stack_tail] = sb;
            last_stack[sb_stack_tail] = prev;
            sb = new->next_superblock;
            kassert(sb);
            kassert(sb->funcs);
            kassert(sb->funcs->lookup);
        }
        if (prev != NULL) close_inode(prev);
        prev = new;
        final_path = next_slash + 1;
    }

    spinlock_acquire(&kernel_fd_lock);
    
    int fd = -1;
    for (int i = 0; i < FD_LIMIT_PROCESS; i++) {
        if (!current_process->fds[i]) {
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        spinlock_release(&kernel_fd_lock);
        ret = EMFILE;
        goto err;
    }

    file_descriptor_t * file = get_free_fd();
    if (!file) {
        spinlock_release(&kernel_fd_lock);
        ret = ENFILE;
        goto err;
    }

    file->inode = new;
    file->mode = mode;
    current_process->fds[fd] = file;
    ret = fd;
    spinlock_release(&kernel_fd_lock);

    err:
    spinlock_release(&mount_tree_lock);
    kfree(dup_path);
    return ret;
}