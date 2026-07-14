#include "kernel.h"
#include "block/ata/ata.h"
#include "block/ata/ata_identify.h"
#include "block/ata/ata_commands.h"
#include "lowlevel.h"

#define dkprintf(format, ...) kprintf("ATA: "format, ##__VA_ARGS__)

static void ata_parse_drive_identify(struct ata_drive * drive) {
    if (!drive) return;
    if (!drive->present) return;
    if (!drive->identify_block) {
        drive->present = 0;
        return;
    }

    // since all reserved bytes are 0, this is a safe assumption
    // I don't think there's a better way anyway?
    drive->has_lba   = drive->identify_block->capabilities.lba_supported;
    drive->has_lba48 = drive->identify_block->command_set_supported2.lba48;
    drive->has_dma   = drive->identify_block->capabilities.dma_supported;

    // very cheeky :3
    drive->ata_version = 31 - __builtin_clz(drive->identify_block->__supported_major_versions);

    if (drive->ata_version < 6) drive->has_lba48 = 0; // LBA 48 began in ATA-6, can't have it before, can it?

    dkprintf("Drive, ATA version %d:\n", drive->ata_version);
    kprintf("\tName: ");
    for (int i = 0; i < sizeof(drive->identify_block->model_number); i+=2) {
        kprintf("%c%c",
            drive->identify_block->model_number[i + 1], drive->identify_block->model_number[i]);
    }
    kprintf("\n\tFirmware: ");
    for (int i = 0; i < sizeof(drive->identify_block->firmware_revision); i+=2) {
        kprintf("%c%c",
            drive->identify_block->firmware_revision[i + 1], drive->identify_block->firmware_revision[i]);
    }
    kprintf("\n\tSerial: ");
    for (int i = 0; i < sizeof(drive->identify_block->serial_number); i+=2) {
        kprintf("%c%c",
            drive->identify_block->serial_number[i + 1], drive->identify_block->serial_number[i]);
    }

    drive->sector_size = drive->identify_block->bytes_per_sector;

    // some really old harddrives, and vmware apparently, set this to 0
    if (drive->sector_size == 0)
        drive->sector_size = 512;

    drive->sector_count = drive->identify_block->current_sectors_per_track *
                          drive->identify_block->current_head_count *
                          drive->identify_block->current_cylinder_count;
    if (drive->has_lba) {
        drive->sector_count = drive->identify_block->total_user_lba;
    }
    if (drive->has_lba48) {
        if (drive->identify_block->total_user_lba48 < 1 << 28) // lba28 is slightly faster
            drive->has_lba48 = 0;
        else
            drive->sector_count = drive->identify_block->total_user_lba48;
    }

    uint64_t hdd_byte_size = drive->sector_size * drive->sector_count;



    kprintf("\n\tSize %llu MiB @ %d sector size\n", hdd_byte_size / 1024 / 1024, drive->sector_size);
}

#define ATAPI_SIG_SEC_COUNT 0x01
#define ATAPI_SIG_LBA_LO    0x01
#define ATAPI_SIG_LBA_MID   0x14
#define ATAPI_SIG_LBA_HI    0xEB
struct ata_identify * ata_identify(unsigned char bus_id, unsigned char drive_number) {
    kassert(bus_id < 2);
    kassert(drive_number < 2);
    if (!ata_buses[bus_id].is_initialized)               return NULL;
    if (!ata_buses[bus_id].drives[drive_number].present) return NULL;

    // reset values so that ATAPI signature detection works
    ata_select_drive(bus_id, drive_number);
    outb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_COUNT, 0);
    outb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_NUM, 0);
    outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_LOW, 0);
    outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_HIGH, 0);

    ata_bus_soft_reset(bus_id);

    if (
        inb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_COUNT)  == ATAPI_SIG_SEC_COUNT &&
        inb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_NUM)    == ATAPI_SIG_LBA_LO &&
        inb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_LOW)  == ATAPI_SIG_LBA_MID &&
        inb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_HIGH) == ATAPI_SIG_LBA_HI
    ) {
        dkprintf("Potential ATAPI device on bus %d drive %d, no driver support yet\n", bus_id, drive_number);
        ata_buses[bus_id].drives[drive_number].present = 0;
        return NULL;
    }


    if (!ata_send_command(bus_id, drive_number, ATA_IDENTIFY_DEVICE)) {
        return NULL;
    }

    kassert(sizeof(struct ata_identify) == ATA_IDENTIFY_BLOCK_SIZE);

    struct ata_identify * identify = kalloc(sizeof(struct ata_identify));
    kassert(identify);

    ata_read_pending_block(bus_id, identify, ATA_IDENTIFY_BLOCK_SIZE );
    ata_buses[bus_id].drives[drive_number].identify_block = identify;
    ata_parse_drive_identify(&ata_buses[bus_id].drives[drive_number]);
    return identify;
}