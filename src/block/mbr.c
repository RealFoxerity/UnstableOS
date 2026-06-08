#include "fs/fs.h"
#include "dev_ops.h"
#include <UnstableOS/devs.h>
#include "kernel.h"
#include <errno.h>
#include <stdint.h>

#include "block/partitions.h"

#define dkprintf(fmt, ...) kprintf("mbr: " fmt, ##__VA_ARGS__)

struct mbr_chs_address {
    uint8_t head;
    uint8_t sector : 6;
    uint8_t cyl_hi : 2;
    uint8_t cyl_lo;
} __attribute__((packed));

struct mbr_entry {
    uint8_t attributes; // 0x80 = bootable
    struct mbr_chs_address start_chs;
    uint8_t type;
    struct mbr_chs_address end_chs;
    uint32_t start_lba;
    uint32_t sector_count;
} __attribute__((packed));

struct mbr {
    uint8_t bootstrap[446];
    struct mbr_entry partitions[4];
    uint16_t magic;
} __attribute__((packed));

#define MBR_MAGIC 0xAA55 // little endian
#define MBR_SECTOR_SIZE 512 // emulate 512 byte sectors even on 4K drives

#define MBR_PART_TYPE_EXT_CHS 0x5
#define MBR_PART_TYPE_EXT_LBA 0xF
#define MBR_PART_TYPE_UNUSED 0x0

// we don't yet support CHS
// to be fair, CHS tops out at 8GB, so probably irrelevant anyway
long mbr_parse_table(dev_t drive) {
    file_descriptor_t * drive_file = NULL;
    long ret = open_raw_device(drive, O_RDONLY, &drive_file);
    if (ret < 0) return ret;
    if (drive_file == NULL) return -EINVAL;

    struct mbr table = {0};
    ssize_t read_bytes = 0;
    if ((read_bytes = pread_file(drive_file, &table, sizeof(struct mbr), 0)) != sizeof(struct mbr)) {
        close_file(drive_file);
        if (read_bytes < 0)
            return read_bytes;
        return 0;
    }

    if (table.magic != MBR_MAGIC)
        return 0;

    for (int i = 1; i < DRIVE_PART_LIMIT; i++) {
        if (part_del(drive + i) != 0) {
            char device_name[32];
            if (dev2string(drive + i, device_name) == NULL) {
                dkprintf("Warning: can't remove partition %d of dev %hx\n", i, drive);
            } else {
                dkprintf("Warning: can't remove partition %s\n", device_name);
            }
        }
    }

    int last_part_no = 1;
    char seen_extended = 0;
    for (int i = 0; i < 4; i++) {
        if (table.partitions[i].type == MBR_PART_TYPE_UNUSED)
            continue;
        if (table.partitions[i].sector_count == 0)
            continue;
        // illegal, lba 0 is the table itself
        if (table.partitions[i].start_lba == 0)
            continue;

        off_t ext_part_start = table.partitions[i].start_lba * MBR_SECTOR_SIZE;
        switch (table.partitions[i].type) {
            case MBR_PART_TYPE_EXT_CHS:
            case MBR_PART_TYPE_EXT_LBA:
                if (seen_extended) {
                    dkprintf("Warning: multiple extended partitions!\n");
                }
                seen_extended++;

                while (1) {
                    struct mbr extended_table = {0};
                    if (pread_file(drive_file, &extended_table, sizeof(struct mbr), ext_part_start) != sizeof(struct mbr))
                        break;
                    if (extended_table.magic != MBR_MAGIC) {
                        dkprintf("Warning: extended partition pointing to invalid header!\n");
                        break;
                    }
                    if (extended_table.partitions[0].sector_count != 0 &&
                        extended_table.partitions[0].start_lba != 0 &&
                        extended_table.partitions[0].type != MBR_PART_TYPE_UNUSED)
                    {
                        switch (extended_table.partitions[0].type) {
                            case MBR_PART_TYPE_EXT_LBA:
                            case MBR_PART_TYPE_EXT_CHS:
                                dkprintf("Warning: unexpected extended partition at sub entry 0!\n");
                                break;
                            default:
                                if (last_part_no >= DRIVE_PART_LIMIT) {
                                    char device_name[32];
                                    if (dev2string(drive + i, device_name) == NULL) {
                                        dkprintf("Warning: ran out of assignable dev minors for partitions on dev %hx\n", drive);
                                    } else {
                                        dkprintf("Warning: ran out of assignable dev minors for partitions on %s\n", device_name);
                                    }
                                    return 0;
                                }
                                if (part_add(drive + last_part_no, (struct partition) {
                                        .start = ext_part_start + extended_table.partitions[0].start_lba * MBR_SECTOR_SIZE,
                                        .size = extended_table.partitions[0].sector_count * MBR_SECTOR_SIZE,
                                    }) != 0) {
                                    kprintf("mbr: Warning: can't add extended partition %d (lba %llu, sector count %lu) of dev %hx\n",
                                        i + 1, (ext_part_start + MBR_SECTOR_SIZE - 1)/MBR_SECTOR_SIZE + extended_table.partitions[0].start_lba,
                                        extended_table.partitions[0].sector_count, drive);
                                }
                                last_part_no++;
                                break;
                        }
                    }
                    if (extended_table.partitions[1].sector_count != 0 &&
                        extended_table.partitions[1].start_lba != 0 &&
                        extended_table.partitions[1].type != MBR_PART_TYPE_UNUSED)
                    {
                        if (extended_table.partitions[1].type == MBR_PART_TYPE_EXT_CHS ||
                            extended_table.partitions[1].type == MBR_PART_TYPE_EXT_LBA) {
                            ext_part_start += extended_table.partitions[1].start_lba * MBR_SECTOR_SIZE;
                        } else {
                            dkprintf("Warning: unexpected non-extended partition at sub entry 1!\n");
                            break;
                        }
                    } else
                        break;
                }
                break;
            default:
                if (last_part_no >= DRIVE_PART_LIMIT) {
                    char device_name[32];
                    if (dev2string(drive + i, device_name) == NULL) {
                        dkprintf("Warning: ran out of assignable dev minors for partitions on dev %hx\n", drive);
                    } else {
                        dkprintf("Warning: ran out of assignable dev minors for partitions on %s\n", device_name);
                    }
                    return 0;
                }
                if (part_add(drive + last_part_no,
                    (struct partition) {
                        .start = table.partitions[i].start_lba * MBR_SECTOR_SIZE,
                        .size = table.partitions[i].sector_count * MBR_SECTOR_SIZE,
                    }) != 0)
                {
                    char device_name[32];
                    if (dev2string(drive, device_name) == NULL) {
                        kprintf("mbr: Warning: can't add partition %d (lba %lu, sector count %lu) of dev %hx\n",
                            i + 1, table.partitions[i].start_lba, table.partitions[i].sector_count, drive);
                    } else {
                        kprintf("mbr: Warning: can't add partition %s (lba %lu, sector count %lu)\n",
                            device_name, table.partitions[i].start_lba, table.partitions[i].sector_count);
                    }

                }
                last_part_no++;
        }
    }
}