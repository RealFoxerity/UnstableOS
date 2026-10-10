#include "kernel.h"
#include "kernel_spinlock.h"
#include "mm/kernel_memory.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "rbtree.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define TMPFS_NAME_MAX 255
#define TMPFS_START_ROOT_PERMS 01777
// this implementation assumes there will never be >SIZE_MAX files on a single tmpfs

struct tmpfile_rbtree {
    rbtree_t tree;
    void * paddr;
};
struct tmpfile_t {
    char name[TMPFS_NAME_MAX+1];

    size_t ino;

    mode_t mode;
    dev_t dev; // for S_IFCHR and S_IFBLK

    uid_t uid;
    gid_t gid;

    time_t btime, ctime, mtime, atime;

    size_t size;
    size_t alloced_pages;
    struct tmpfile_rbtree * pages;
    rw_spinlock_t page_lock; // so browsing and reading don't interfere

    union {
        struct {
            char used; // used by unlink to either immediately remove resources or defer
            char unlinked; // to allow usage after/during unlink
        };
        unsigned short _uu;
    };
    rw_spinlock_t lock; // acquire read to browse children
    struct tmpfile_t *parent, *children;
    struct tmpfile_t *prev, *next;
} typedef tmpfile_t;

struct tmpfs_sb {
    tmpfile_t * root;
    size_t last_ino;
    size_t used_pages;
    size_t files;
};

static int tmpfs_init(superblock_t * sb) {
    struct tmpfs_sb * tmpfs = kalloc(sizeof(struct tmpfs_sb));
    if (!tmpfs)
        return -ENOMEM;
    tmpfile_t * root = kalloc(sizeof(tmpfile_t));
    if (!root) {
        kfree(tmpfs);
        return -ENOMEM;
    }
    memset(tmpfs, 0, sizeof(struct tmpfs_sb));
    memset(root, 0, sizeof(tmpfile_t));
    tmpfs->last_ino = 1;

    root->btime  = root->ctime = root->mtime = root->atime = system_time_sec;
    root->mode   = S_IFDIR | TMPFS_START_ROOT_PERMS;
    root->parent = root;

    tmpfs->root  = root;

    sb->data = tmpfs;
    return 0;
}

static void tmpfs_free_file_data(struct tmpfs_sb * tmpfs, struct tmpfile_rbtree * data) {
    if (data == NULL)
        return;
    tmpfs_free_file_data(tmpfs, (void*)data->tree.nodes[0]);
    tmpfs_free_file_data(tmpfs, (void*)data->tree.nodes[1]);
    pffree(data->paddr);
    kfree(data);

    __atomic_sub_fetch(&tmpfs->used_pages, 1, __ATOMIC_RELAXED);
}

static void tmpfs_free_file(struct tmpfs_sb * tmpfs, tmpfile_t * file) {
    if (file == NULL)
        return;

    kassert(!file->children);

    tmpfs_free_file_data(tmpfs, file->pages);
    kfree(file);
    __atomic_sub_fetch(&tmpfs->files, 1, __ATOMIC_RELAXED);
}

static void tmpfs_free_tree(struct tmpfs_sb * tmpfs, tmpfile_t * file) {
    if (file == NULL)
        return;
    tmpfs_free_tree(tmpfs, file->children);
    tmpfs_free_tree(tmpfs, file->next);
    tmpfs_free_file(tmpfs, file);
}

static int tmpfs_deinit(superblock_t * sb) {
    struct tmpfs_sb * tmpfs = sb->data;
    tmpfs_free_tree(tmpfs, tmpfs->root);
    kfree(tmpfs);
    return 0;
}

static tmpfile_t * tmpfs_lookup_internal(tmpfile_t * dir, const char * pathname) {
    if (!dir)
        return NULL;
    dir = dir->children;
    while (dir) {
        if (strcmp(pathname, dir->name) == 0)
            break;
        dir = dir->next;
    }
    return dir;
}

static void tmpfs_inode_from_file(tmpfile_t * file, inode_t * out) {
    out->id = file->ino;
    out->data = file;

    out->mode = file->mode;
    if (S_ISCHR(file->mode) || S_ISBLK(file->mode))
        out->device = file->dev;
    out->uid = file->uid;
    out->gid = file->gid;

    out->size = file->size;
    out->io_block_size = PAGE_SIZE;
    out->block_count = file->alloced_pages;

    out->btime = file->btime;
    out->ctime = file->ctime;
    out->mtime = file->mtime;
    out->atime = file->atime;

    out->nlink = 1;
}

static int tmpfs_lookup(superblock_t * sb, inode_t * last, const char * pathname, inode_t ** inode_out, unsigned short flags) {
    kassert(sb);
    kassert(pathname);
    kassert(inode_out);

    struct tmpfs_sb * tmpfs = sb->data;
    kassert(tmpfs);

    inode_t new = {0};
    char locked = 0;
    tmpfile_t * dir = last ? last->data : tmpfs->root;
    tmpfile_t * file = NULL;
    if (!dir)
        return -ENOENT;

    if (dir->unlinked)
        return -ENOENT;

    if (strcmp(pathname, ".") == 0) {
        if (last) {
            *inode_out = last;
            __atomic_add_fetch(&last->instances, 1, __ATOMIC_ACQUIRE);
            return 0;
        }
        file = dir;
        goto end;
    }
    if (strcmp(pathname, "..") == 0) {
        if (!last || dir == tmpfs->root)
            return VFS_LOOKUP_ESCAPE;
        file = dir->parent;
        goto end;
    }

    if (strnlen(pathname, TMPFS_NAME_MAX+1) > TMPFS_NAME_MAX)
        return -ENOENT;

    rw_spinlock_acquire_read(&dir->lock);
    locked = 1;
    file = tmpfs_lookup_internal(dir, pathname);
    if (!file) {
        rw_spinlock_release_read(&dir->lock);
        return -ENOENT;
    }

    end:
    tmpfs_inode_from_file(file, &new);
    new.backing_superblock = sb;
    __atomic_store_n(&file->used, 1, __ATOMIC_RELEASE);

    int ret = register_inode(&new, inode_out, flags);
    if (locked) // we need to increment the instance counters to not race with unlink/rename
        rw_spinlock_release_read(&dir->lock);
    return ret;
}

static int tmpfs_release(inode_t * inode) {
    kassert(inode);
    kassert(inode->backing_superblock);
    kassert(inode->backing_superblock->data);
    kassert(inode->data);
    tmpfile_t * file = inode->data;
    struct tmpfs_sb * tmpfs = inode->backing_superblock->data;

    file->mode = inode->mode;

    file->uid = inode->uid;
    file->gid = inode->gid;

    file->btime = inode->btime;
    file->ctime = inode->ctime;
    file->mtime = inode->mtime;
    file->atime = inode->atime;

    file->size = inode->size;
    // will never be >ULONG_MAX, this way it makes pwrite not require a lock
    file->alloced_pages = __atomic_load_n((unsigned long*)&inode->block_count, __ATOMIC_ACQUIRE);
    __atomic_store_n(&file->used, 0, __ATOMIC_RELEASE);

    // we need this load below the used store for unlink to work properly
    if (__atomic_load_n(&file->unlinked, __ATOMIC_ACQUIRE)) {
        if (file == tmpfs->root)
            panic("tmpfs: unlinked root!\n");
        if (file->children)
            panic("tmpfs: nonempty unlinked folder!\n");

        tmpfs_free_file(inode->backing_superblock->data, file);
    }
    return 0;
}

static int tmpfs_creat_internal(inode_t * parent, const char * pathname, mode_t mode, inode_t ** inode_out, dev_t dev) {
    kassert(parent);
    kassert(parent->backing_superblock);
    kassert(parent->backing_superblock->data);
    kassert(parent->data);

    struct tmpfs_sb * tmpfs = parent->backing_superblock->data;
    tmpfile_t * dir = parent->data;

    if (strnlen(pathname, TMPFS_NAME_MAX+1) > TMPFS_NAME_MAX)
        return -ENAMETOOLONG;

    // read lock is less costly, so better for the preliminary check
    rw_spinlock_acquire_read(&dir->lock);
    if (tmpfs_lookup_internal(dir, pathname)) {
        rw_spinlock_release_read(&dir->lock);
        return -EEXIST;
    }
    rw_spinlock_release_read(&dir->lock);

    rw_spinlock_acquire_write(&dir->lock);
    // obviously we could've raced
    if (tmpfs_lookup_internal(dir, pathname)) {
        rw_spinlock_release_write(&dir->lock);
        return -EEXIST;
    }


    tmpfile_t * new = kalloc(sizeof(tmpfile_t));
    if (!new) {
        rw_spinlock_release_write(&dir->lock);
        return -ENOSPC;
    }
    __atomic_add_fetch(&tmpfs->files, 1, __ATOMIC_RELAXED);

    memset(new, 0, sizeof(tmpfile_t));
    strcpy(new->name, pathname);
    new->ino = __atomic_add_fetch(&tmpfs->last_ino, 1, __ATOMIC_ACQUIRE);

    new->mode = mode;
    if (S_ISCHR(mode) || S_ISBLK(mode))
        new->dev = dev;

    new->uid = current_process->euid;
    new->gid = current_process->gid;

    new->btime = new->ctime = new->mtime = new->atime = system_time_sec;

    new->parent = dir;

    APPEND_DOUBLE_LINKED_LIST(new, dir->children);

    inode_t out;
    tmpfs_inode_from_file(new, &out);
    out.backing_superblock = parent->backing_superblock;
    if (inode_out) {
        int ret = register_inode(&out, inode_out, 0);
        if (ret >= 0)
            __atomic_store_n(&new->used, 1, __ATOMIC_RELEASE);
        rw_spinlock_release_write(&dir->lock);
        return ret;
    }

    rw_spinlock_release_write(&dir->lock);
    return 0;
}

static int tmpfs_creat(inode_t * parent, const char * pathname, mode_t mode, inode_t ** inode_out) {
    mode &= ~S_IFMT;
    mode |= S_IFREG;
    return tmpfs_creat_internal(parent, pathname, mode, inode_out, 0);
}

static int tmpfs_mkdir(inode_t * parent, const char * pathname, mode_t mode, inode_t ** inode_out) {
    mode &= ~S_IFMT;
    mode |= S_IFDIR;
    return tmpfs_creat_internal(parent, pathname, mode, inode_out, 0);
}

static int tmpfs_mknod(inode_t * parent, const char * pathname, mode_t mode, dev_t dev) {
    mode &= ~S_IFMT | S_IFCHR | S_IFBLK;
    if (!(mode & (S_IFCHR | S_IFBLK)))
        return -EINVAL;
    return tmpfs_creat_internal(parent, pathname, mode, NULL, dev);
}

static int tmpfs_unlink(inode_t * parent, const char * name) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -EINVAL;
    kassert(parent);
    kassert(parent->backing_superblock);
    kassert(parent->backing_superblock->data);
    kassert(parent->data);

    if (!S_ISDIR(parent->mode))
        return -ENOTDIR;

    if (strnlen(name, TMPFS_NAME_MAX+1) > TMPFS_NAME_MAX)
        return -ENOENT;

    tmpfile_t * dir = parent->data;
    tmpfile_t * file = NULL;
    struct tmpfs_sb * tmpfs = parent->backing_superblock->data;

    rw_spinlock_acquire_read(&dir->lock);
    file = tmpfs_lookup_internal(dir, name);
    rw_spinlock_release_read(&dir->lock);
    if (file == NULL)
        return -ENOENT;
    if (file->children)
        return -ENOTEMPTY;

    rw_spinlock_acquire_write(&dir->lock);
    // race on every condition possible
    file = tmpfs_lookup_internal(dir, name);
    if (file == NULL) {
        rw_spinlock_release_write(&dir->lock);
        return -ENOENT;
    }
    if (file->children) {
        rw_spinlock_release_write(&dir->lock);
        return -ENOTEMPTY;
    }

    UNLINK_DOUBLE_LINKED_LIST(file, file->parent->children);
    rw_spinlock_release_write(&dir->lock);

    union {
        struct {
            char used; // used by unlink to either immediately remove resources or defer
            char unlinked; // to allow usage after/during unlink
        };
        unsigned short _uu;
    } u;

    u.used = 1;
    u.unlinked = 1;

    __atomic_exchange(&file->_uu, &u._uu, &u._uu, __ATOMIC_ACQUIRE);
    // atomic here because otherwise u.used != 1 might get optimized away
    if (__atomic_load_n(&u.used, __ATOMIC_RELAXED) != 1)
        tmpfs_free_file(tmpfs, file);
    return 0;
}

static int tmpfs_rename(inode_t * old, const char * oldname, inode_t * new, const char * newname) {
    if (strcmp(oldname, ".") == 0 || strcmp(oldname, "..") == 0)
        return -EINVAL;
    if (strcmp(newname, ".") == 0 || strcmp(newname, "..") == 0)
        return -EINVAL;

    if (strnlen(oldname, TMPFS_NAME_MAX+1) > TMPFS_NAME_MAX)
        return -ENOENT;
    if (strnlen(newname, TMPFS_NAME_MAX+1) > TMPFS_NAME_MAX)
        return -ENAMETOOLONG;

    kassert(old);
    kassert(old->backing_superblock);
    kassert(old->backing_superblock->data);
    kassert(old->data);

    kassert(new);
    kassert(new->backing_superblock == old->backing_superblock);
    kassert(new->data);

    struct tmpfs_sb * tmpfs = old->backing_superblock->data;
    tmpfile_t * oldparent = old->data;
    tmpfile_t * newparent = new->data;

    tmpfile_t *oldfile = NULL, *newfile = NULL;

    rw_spinlock_acquire_read(&oldparent->lock);
    oldfile = tmpfs_lookup_internal(oldparent, oldname);
    rw_spinlock_release_read(&oldparent->lock);
    if (oldfile == NULL)
        return -ENOENT;

    rw_spinlock_acquire_read(&newparent->lock);
    newfile = tmpfs_lookup_internal(newparent, newname);
    if (newfile && newfile->children) {
        rw_spinlock_release_read(&newparent->lock);
        return -ENOTEMPTY;
    }
    rw_spinlock_release_read(&newparent->lock);

    rw_spinlock_acquire_write(&oldparent->lock);
    oldfile = tmpfs_lookup_internal(oldparent, oldname);
    // raced on unlink or another rename
    if (oldfile == NULL) {
        rw_spinlock_release_write(&oldparent->lock);
        return -ENOENT;
    }
    if (oldparent != newparent)
        rw_spinlock_acquire_write(&newparent->lock);

    newfile = tmpfs_lookup_internal(newparent, newname);
    // raced on unlink or another rename
    if (newfile && newfile->children) {
        rw_spinlock_release_write(&newparent->lock);
        if (oldparent != newparent)
            rw_spinlock_release_write(&oldparent->lock);
        return -ENOTEMPTY;
    }
    if (newfile)
        UNLINK_DOUBLE_LINKED_LIST(newfile, newparent->children);

    UNLINK_DOUBLE_LINKED_LIST(oldfile, oldparent->children);
    if (oldparent != newparent)
        rw_spinlock_release_write(&oldparent->lock);

    strcpy(oldfile->name, newname);
    APPEND_DOUBLE_LINKED_LIST(oldfile, newparent->children);

    rw_spinlock_release_write(&newparent->lock);

    if (newfile) {
        // copy of tmpfs_unlink, can't be nicely factored out (would be a long held parent lock) unfortunately
        union {
            struct {
                char used; // used by unlink to either immediately remove resources or defer
                char unlinked; // to allow usage after/during unlink
            };
            unsigned short _uu;
        } u;

        u.used = 1;
        u.unlinked = 1;

        __atomic_exchange(&newfile->_uu, &u._uu, &u._uu, __ATOMIC_ACQUIRE);
        // atomic here because otherwise u.used != 1 might get optimized away
        if (__atomic_load_n(&u.used, __ATOMIC_RELAXED) != 1)
            tmpfs_free_file(tmpfs, newfile);
    }
    return 0;
}

static ssize_t tmpfs_readdir(file_descriptor_t * fd, struct dirent * dent, size_t dent_size, off_t offset) {
    if (!dent)
        return -EFAULT;
    if (offset < 0)
        return -ENOENT;
    // it should be somewhat obvious and logical that on a 32 bit machine,
    // there cannot ever be more than this anywhere in memory
    if (offset > SIZE_MAX / sizeof(tmpfile_t))
        return -ENOENT;
    if (dent_size < sizeof(struct dirent) + 2)
        return -EINVAL;

    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->data);

    char locked = 0;

    tmpfile_t * file = fd->inode->data;
    if (offset == 1)
        file = file->parent;

    if (offset == 0) {
        strcpy(dent->d_name, ".");
        dent->d_reclen = sizeof(struct dirent) + sizeof(".");
        goto end;
    }
    if (offset == 1) {
        strcpy(dent->d_name, "..");
        dent->d_reclen = sizeof(struct dirent) + sizeof("..");
        goto end;
    }

    tmpfile_t * parent = file;
    file = file->children;
    rw_spinlock_acquire_read(&parent->lock);
    locked = 1;
    for (size_t i = 0; i < offset - 2 && file; i++)
        file = file->next;

    if (!file) {
        rw_spinlock_release_read(&parent->lock);
        return 0;
    }

    size_t name_len = strlen(file->name);
    if (dent_size < sizeof(struct dirent) + name_len + 1) {
        rw_spinlock_release_read(&parent->lock);
        return -EINVAL;
    }

    strcpy(dent->d_name, file->name);

    dent->d_reclen = sizeof(struct dirent) + name_len + 1;

    end:
    dent->d_ino = file->ino;
    dent->d_off = offset;
    dent->d_type = IFTODT(file->mode & S_IFMT);

    if (locked)
        rw_spinlock_release_read(&parent->lock);

    rw_spinlock_acquire_write(&fd->access_lock);
    fd->off = offset + 1;
    rw_spinlock_release_write(&fd->access_lock);

    return dent->d_reclen;
}

#define TMPFS_SCRATCH_START ((void*)0xFF000000)
#define TMPFS_SCRATCH_PAGES 512

static unsigned int tmpfs_bitmap[TMPFS_SCRATCH_PAGES/(8*sizeof(unsigned int))] = {0};

// increment/decrement page frame counters before/after tmpfs_map/unmap_page
// done this way so tmpfs_map_page doesn't require file locking (after getting the paddr)
__attribute__((returns_nonnull)) static void * tmpfs_map_page(void * paddr) {
    while (1) {
        for (int i = 0; i < TMPFS_SCRATCH_PAGES/(8*sizeof(unsigned int)); i++) {
            unsigned int expected = __atomic_load_n(&tmpfs_bitmap[i], __ATOMIC_ACQUIRE);
            while (~expected) {
                unsigned int free_idx = bsf(~expected) - 1;
                unsigned int wanted = expected | (1 << free_idx);
                if (__atomic_compare_exchange(
                    &tmpfs_bitmap[i],
                    &expected, &wanted,
                    0,
                    __ATOMIC_RELEASE,  __ATOMIC_RELAXED)
                ) {
                    void * target_addr = TMPFS_SCRATCH_START +
                        (i*8*sizeof(unsigned int) + free_idx) * PAGE_SIZE;
                    paging_map_phys_addr(paddr, target_addr, PTE_PDE_PAGE_WRITABLE);
                    return target_addr;
                }
            }
        }
        // reading a page worth of data is pretty fast
        // reschedule should be more than enough
        reschedule();
    }
}

static void tmpfs_unmap_page(void * vaddr) {
    paging_unmap_page(vaddr);

    unsigned int * bucket = tmpfs_bitmap + ((vaddr - TMPFS_SCRATCH_START)/(8*sizeof(unsigned int)));
    unsigned int idx = (vaddr - TMPFS_SCRATCH_START) % (8*sizeof(unsigned int));

    __atomic_and_fetch(bucket, ~(1 << idx), __ATOMIC_RELEASE);
}

ssize_t tmpfs_pread(file_descriptor_t * fd, void * buf, size_t n, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->data);
    if (!buf)
        return -EFAULT;

    if (offset < 0) return -EINVAL;
    if (!S_ISREG(fd->inode->mode)) return -EINVAL;
    if (offset >= fd->inode->size) return 0;
    if (n == 0) return 0;
#ifdef E2BIG_ON_2G
    if (n > SSIZE_MAX) return -E2BIG;
#else
    if (n > SSIZE_MAX) n = SSIZE_MAX;
#endif

    if (check_eintr())
        return -EINTR;

    tmpfile_t * file = fd->inode->data;
    size_t read_bytes = 0;

    rw_spinlock_acquire_read(&file->page_lock);
    while (read_bytes < n && offset < fd->inode->size) {
        size_t remaining_of_page = PAGE_SIZE - offset % PAGE_SIZE;
        size_t to_read           = remaining_of_page > n - read_bytes ? n - read_bytes : remaining_of_page;
        if (offset + to_read > fd->inode->size)
            to_read = fd->inode->size - offset;

        struct tmpfile_rbtree * page =
            (void*)rbtree_search_exact((rbtree_t*)file->pages, offset & ~(PAGE_SIZE - 1));
        if (!page) {
            memset(buf, 0, to_read);
            if (check_eintr()) {
                rw_spinlock_release_read(&file->page_lock);
                break;
            }
            goto cont;
        }

        void * paddr = page->paddr;
        paddr = pfalloc_ref_inc(paddr);
        rw_spinlock_release_read(&file->page_lock);

        if (!paddr)
            return -EIO; // what to return here?

        void * vaddr = tmpfs_map_page(paddr);
        memcpy(buf, vaddr + offset % PAGE_SIZE, to_read);

        tmpfs_unmap_page(vaddr);
        pffree(paddr);

        if (check_eintr())
            break;

        rw_spinlock_acquire_read(&file->page_lock);
        cont:
        offset += to_read;
        read_bytes += to_read;
        buf += to_read;
    }

    rw_spinlock_release_read(&file->page_lock);
    return (ssize_t)read_bytes;
}
ssize_t tmpfs_pwrite(file_descriptor_t * fd, const void * buf, size_t n, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    kassert(fd->inode->data);
    if (!buf)
        return -EFAULT;

    if (offset < 0) return -EINVAL;
    if (!S_ISREG(fd->inode->mode)) return -EINVAL;
    if (offset > ULONG_MAX) return 0;
    if (n == 0) return 0;
#ifdef E2BIG_ON_2G
    if (n > SSIZE_MAX) return -E2BIG;
#else
    if (n > SSIZE_MAX) n = SSIZE_MAX;
#endif

    if (offset + n > ULONG_MAX)
        n = ULONG_MAX - n;

    if (check_eintr())
        return -EINTR;

    tmpfile_t * file = fd->inode->data;
    size_t written_bytes = 0;
    ssize_t ret = 0;

    rw_spinlock_acquire_read(&file->page_lock);
    while (written_bytes < n) {
        size_t remaining_of_page = PAGE_SIZE - offset % PAGE_SIZE;
        size_t to_write          = remaining_of_page > n - written_bytes ? n - written_bytes : remaining_of_page;

        struct tmpfile_rbtree * page =
            (void*)rbtree_search_exact((rbtree_t*)file->pages, offset & ~(PAGE_SIZE - 1));
        if (!page) {
            rw_spinlock_release_read(&file->page_lock);
            if (check_eintr()) // this is going to take a while
                break;
            rw_spinlock_acquire_write(&file->page_lock);
            page = (void*)rbtree_search_exact((rbtree_t*)file->pages, offset & ~(PAGE_SIZE - 1));
            if (page)
                goto ok;

            page = kalloc(sizeof(struct tmpfile_rbtree));
            void * free_paddr = NULL;
            if (!page || !((free_paddr = pfalloc()))) {
                rw_spinlock_release_write(&file->page_lock);
                kfree(page);
                ret = -ENOSPC;
                goto end;
            }
            // this will never reach into the high 4 bytes, so this aliasing is safe
            __atomic_add_fetch((unsigned long*)&fd->inode->block_count, 1, __ATOMIC_RELAXED);

            page->paddr = free_paddr;
            page->tree.val = (unsigned long)(offset & ~(PAGE_SIZE - 1));
            rbtree_add((rbtree_t**)&file->pages, (rbtree_t*)page);
            rw_spinlock_downgrade(&file->page_lock);
        }

        ok:
        void * paddr = page->paddr;
        paddr = pfalloc_ref_inc(paddr);
        rw_spinlock_release_read(&file->page_lock);

        if (!paddr) {
            ret = -ENOSPC;
            goto end;
        }

        void * vaddr = tmpfs_map_page(paddr);
        memcpy(vaddr + offset % PAGE_SIZE, buf, to_write);

        tmpfs_unmap_page(vaddr);
        pffree(paddr);

        if (check_eintr())
            break;

        rw_spinlock_acquire_read(&file->page_lock);

        offset += to_write;
        written_bytes += to_write;
        buf += to_write;

        if (written_bytes >= n) // next iter will break out
            rw_spinlock_release_read(&file->page_lock);
    }

    ret = written_bytes == 0 ? -EINTR : (ssize_t)written_bytes;

    end:
    spinlock_acquire(&fd->inode->lock);
    if (offset > fd->inode->size)
        fd->inode->size = offset;
    spinlock_release(&fd->inode->lock);

    return ret;
}

int tmpfs_trunc(inode_t * file, off_t length) {
    kassert(file);
    kassert(file->data);
    kassert(file->backing_superblock);
    kassert(file->backing_superblock->data);

    if (length < 0)
        return -EINVAL;
    if (!S_ISREG(file->mode))
        return -EINVAL;
    if (length > SIZE_MAX)
        return -E2BIG;

    spinlock_acquire(&file->lock);
    if (length > file->size) {
        file->size = length;
        spinlock_release(&file->lock);
        return 0;
    }
    spinlock_release(&file->lock);

    if (check_eintr())
        return -EINTR;

    tmpfile_t * tmpfile = file->data;
    struct tmpfs_sb * tmpfs = file->backing_superblock->data;

    size_t aligned_size = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    rw_spinlock_acquire_write(&tmpfile->page_lock);
    if (aligned_size == 0) {
        struct tmpfile_rbtree * pages =
            __atomic_exchange_n(&tmpfile->pages, NULL, __ATOMIC_RELEASE);
        tmpfs_free_file_data(tmpfs, pages);
        __atomic_store_n(&tmpfile->alloced_pages, 0, __ATOMIC_RELEASE);
        goto end;
    }

    aligned_size += PAGE_SIZE; // because the rbtree search is gte
    struct tmpfile_rbtree * page = NULL;
    while ((page = (struct tmpfile_rbtree *)rbtree_search_gte(
        (rbtree_t*)tmpfile->pages, aligned_size))) {
        rbtree_remove((rbtree_t**)&tmpfile->pages, (rbtree_t*)page);
        pffree(page->paddr);
        kfree(page);
        __atomic_sub_fetch(&tmpfs->used_pages, 1, __ATOMIC_RELAXED);
        __atomic_sub_fetch(&tmpfile->alloced_pages, 1, __ATOMIC_RELAXED);
    }

    end:
    rw_spinlock_release_write(&tmpfile->page_lock);

    spinlock_acquire(&file->lock);
    file->size = length;
    file->block_count = tmpfile->alloced_pages;
    spinlock_release(&file->lock);
    return 0;
}


const struct vfs_ops tmpfs_op = {
    .fs_init   = tmpfs_init,
    .fs_deinit = tmpfs_deinit,

    .lookup  = tmpfs_lookup,
    .release = tmpfs_release,

    .pread   = tmpfs_pread,
    .pwrite  = tmpfs_pwrite,

    .unlink  = tmpfs_unlink,
    .trunc   = tmpfs_trunc,

    .creat   = tmpfs_creat,
    .mkdir   = tmpfs_mkdir,
    .mknod   = tmpfs_mknod,

    .rename  = tmpfs_rename,

    .readdir = tmpfs_readdir,

    .utimes_supported = 1,
    .chmod_supported = 1,
    .chown_supported = 1,
    .chgrp_supported = 1,
    .block_count_supported = 1,

    .btime_supported = 1,
    .ctime_supported = 1,
    .mtime_supported = 1,
    .atime_supported = 1,

    .unlink_opened_supported = 1,

    .uid_max = ULONG_MAX,
    .gid_max = ULONG_MAX,

    .max_btime = LLONG_MAX,
    .max_mtime = LLONG_MAX,
    .max_ctime = LLONG_MAX,
    .max_atime = LLONG_MAX,

    .min_btime = LLONG_MIN,
    .min_mtime = LLONG_MIN,
    .min_ctime = LLONG_MIN,
    .min_atime = LLONG_MIN,
};