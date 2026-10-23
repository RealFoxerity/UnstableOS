#ifndef FS_PROCFS_PROCFS_H
#define FS_PROCFS_PROCFS_H

/* inode id structure:
 * 63 - 32    | 23 - 16 | 15 - 0
 * pid or 0   | folder  | file
 *
 * if pid == 0, then folder and file reference the main procfs
 * (can't query the kernel)
 * if folder == 0, then it's the root of the given leaf
 * if file == 0, it's the folder itself
 */

#define XFILES \
    X(meminfo, 1) \
    X(uptime, 2) \
    X(cmdline, 3) \
    X(version, 4) \
    X(cpuinfo, 5) \
    X(self, 6)

#define X(m, v) PROCFS_##m = (v),
enum main_files {
    XFILES
};
#undef X

#define XPROCFILES \
    X(comm, 1) \
    X(cmdline, 2) \
    X(cwd, 3) \
    X(exe, 4) \
    X(root, 5) \
    X(status, 6) \
    X(ctty, 7) \
    X(times, 8)

#define X(m, v) PROCFS_PROC_##m = (v),
enum proc_files {
    XPROCFILES
};
#undef X

#define PROCFS_FOLDER_MOUNTS 1
#define PROCFS_PROC_FOLDER_FD 1
#define PROCFS_PROC_FOLDER_FDINFO 2

#define INO_GET_PID(ino) ((ino) >> 32)
#define INO_GET_FOLDER(ino) (((ino) >> 16) & 0xFF)
#define INO_GET_FILE(ino) ((ino) & 0xFFFF)
#define GET_INO(pid, folder, file) (((off_t)(pid)<<32) | ((folder) << 16) | (file))

ssize_t procfs_pread_main(file_descriptor_t * fd, void * buf, size_t n, off_t offset);
ssize_t procfs_pread_proc(file_descriptor_t * fd, void * buf, size_t n, off_t offset);
// if comm = 1, then read only argv[0]
ssize_t procfs_pread_cmdline(process_t * proc, void * buf, size_t n, off_t offset, char comm);
#endif