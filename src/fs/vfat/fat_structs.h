#ifndef FS_VFAT_FAT_STRUCTS_H
#define FS_VFAT_FAT_STRUCTS_H
#include <stdalign.h>

#define FAT_MAGIC 0xAA55

#define FAT32_FSI_MAGIC_1 0x41615252 // AaRR
#define FAT32_FSI_MAGIC_2 0x61417272 // aArr
#define FAT32_FSI_MAGIC_3 FAT_MAGIC
struct fat32_fsinfo {
    unsigned int magic_1;
    unsigned char __resv[480];
    unsigned int magic_2;
    unsigned int last_free_clusters;
    unsigned int last_allocated_cluster;
    unsigned char __resv2[12];
    unsigned int magic_3;
} __attribute__((packed));

// aka fat12/fat16 ebpb
struct drive_info {
    unsigned char drive_number;
    unsigned char reserved;
    unsigned char boot_signature;
    // assuming boot signature is 0x29, then:
    unsigned int volume_id;
    char volume_label[11];
    char file_system_type[8];
} __attribute__((packed));

struct fat_32_ebpb {
    unsigned int sectors_per_fat;
    unsigned short active_fat_idx : 4;
    unsigned short                : 3;
    unsigned short fat_mirrored   : 1;
    unsigned short                : 8;
    unsigned short version;
    unsigned int root_dir_cluster;
    unsigned short fsinfo_sector;
    unsigned short boot_sector_backup_start;
    unsigned char __resv[12];
    struct drive_info drive_info;
} __attribute__((packed));

// I refuse to support anything formatted before MS-DOS 3.31
// full 512 bytes
struct fat_block {
    // basic bpb from DOS 2
    unsigned char __jmpinst[3];
    char oem_name[8];
    unsigned short bytes_per_sector;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char number_of_fats;
    unsigned short root_dir_entries; // only for fat12 and fat16
    unsigned short total_sectors;
    unsigned char media_descriptor;
    unsigned short sectors_per_fat; // 0 on fat32, see fat_32_ebpb.sectors_per_fat

    // bpb extension from DOS 3.31+
    unsigned short sectors_per_track; // for legacy CHS, we'll be dealing with LBA, so ignoring this
    unsigned short heads;
    unsigned int hidden_sectors; // for dealing with partitions in int 13, ignoring this
    unsigned int total_sectors_32;

    union {
        struct drive_info fat12;
        struct fat_32_ebpb fat32;
    };
    alignas(256) unsigned char __resv[254]; // boot code
    unsigned short magic;
} __attribute__((packed));


#define FAT_DENTRY_ATTR_RO     0x01
#define FAT_DENTRY_ATTR_HIDDEN 0x02
#define FAT_DENTRY_ATTR_SYSTEM 0x04
#define FAT_DENTRY_ATTR_VOLLBL 0x08
#define FAT_DENTRY_ATTR_SUBDIR 0x10
#define FAT_DENTRY_ATTR_ARCHIV 0x20
#define FAT_DENTRY_ATTR_DEVICE 0x40

struct fat_time {
    unsigned short seconds : 5; // /2
    unsigned short minutes : 6;
    unsigned short hours   : 5;
};

struct fat_date {
    unsigned short day   : 5;
    unsigned short month : 4;
    unsigned short year  : 7;
};

struct fat_dir_entry {
    char name[11];
    unsigned char attr;
    unsigned char __resv; // mostly random implementation stuff; nt vfat case information?
    unsigned char ctime_10ms; // 0 - 199 since DOS 7.0 with VFAT
    struct fat_time ctime;
    struct fat_date cdate;
    struct fat_date adate; // or uid and gid in DR-DOS
    unsigned short fat32_cluster_hi; // or permission bitmap in DR-DOS
    struct fat_time mtime;
    struct fat_date mdate;
    unsigned short start_cluster;
    unsigned int size;
} __attribute__((packed));


#endif