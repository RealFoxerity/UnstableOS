#ifndef _UNSTABLEOS_MOUNT_H
#define _UNSTABLEOS_MOUNT_H

#define MOUNT_RDONLY 1

int mount(const char * dev_path, const char * mountpoint, const char * type, unsigned short options);
int umount(const char * mountpoint);
#endif