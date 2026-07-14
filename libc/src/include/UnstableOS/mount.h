#ifndef _UNSTABLEOS_MOUNT_H
#define _UNSTABLEOS_MOUNT_H

#define MOUNT_RDONLY 1
#define SUPPORTED_FS_COUNT 3
enum supported_filesystems {
    FS_TARFS,
    FS_DEVFS,
    FS_FAT,
};

int mount(const char * dev_path, const char * mountpoint, unsigned char type, unsigned short options);
int umount(const char * mountpoint);
#endif