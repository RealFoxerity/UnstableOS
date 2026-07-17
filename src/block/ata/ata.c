#include "block/ata/ata.h"
#include "block/ata/ata_commands.h"
#include "block/ata/ata_identify.h"
#include <stdint.h>
#include <time.h>
#include "lowlevel.h"
#include "kernel.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#define dkprintf(format, ...) kprintf("ATA: "format, ##__VA_ARGS__)

// each bus gets its own thread, so no locking of the ata_bus struct should be required

struct ata_bus ata_buses[2] = {0};

void ata_bus_soft_reset(unsigned char bus_id) {
    kassert(bus_id < 2);
    kassert(ata_buses[bus_id].is_initialized);

    struct ata_device_control_register dcr = {
        ._resv2 = 1,
        .software_reset = 1
    };

    outb(ata_buses[bus_id].control_base + ATA_CREGS_DEVICE_CONTROL, dcr.device_control_register);

    time_t old_clicks = uptime_clicks;

    unsigned long eflags;
    // in cas we're the only thread running to give room for the rtc interrupt to fire
    asm volatile ("pushf; pop %0; sti;" : "=R"(eflags));
    while (uptime_clicks < old_clicks + 5) {
        reschedule(); // 5 msec wait, 5 USEC should be enough however
    }
    asm volatile ("pushl %0; popf;" :: "R"(eflags));

    dcr.software_reset = 0;

    outb(ata_buses[bus_id].control_base + ATA_CREGS_DEVICE_CONTROL, dcr.device_control_register);

    ata_buses[bus_id].current_drive = 0;
}

void ata_read_pending_block(unsigned char bus_id, void * buf, unsigned int block_size) {
    kassert(bus_id < 2);
    if (!ata_buses[bus_id].is_initialized)  return;
    kassert(buf);

    asm volatile (
        "rep insw"
        :: "d"(ata_buses[bus_id].data_base + ATA_REGS_DATA), "D"(buf), "c"(block_size/sizeof(uint16_t))
    );
}

void ata_write_pending_block(unsigned char bus_id, const void * buf, unsigned int block_size) {
    kassert(bus_id < 2);
    if (!ata_buses[bus_id].is_initialized) return;
    kassert(buf);

    // now, I would do rep outsw, but according to osdev wiki, I shouldn't do that
    // so here goes a for loop :P
    // here's hoping GCC doesn't optimize it away into rep outsw

    for (int i = 0; i < block_size/sizeof(uint16_t); i++) {
        outw(ata_buses[bus_id].data_base + ATA_REGS_DATA, ((uint16_t*)buf)[i]);
    }
}

static void ata_420ns_sleep(unsigned char bus_id) {
    kassert(bus_id < 2);
    for (int i = 0; i < 14; i++)
        inb(ata_buses[bus_id].control_base + ATA_CREGS_ALTERNATE_STATUS);
}

char ata_select_drive(unsigned char bus_id, unsigned char drive_number) {
    kassert(bus_id < 2);
    kassert(drive_number < 2);
    if (!ata_buses[bus_id].is_initialized)               return -1;
    if (!ata_buses[bus_id].drives[drive_number].present) return -1;

    if (drive_number && ata_buses[bus_id].current_drive) return 0;

    ata_buses[bus_id].current_drive = drive_number;

    struct ata_drive_head_register drive_select = {
        ._resv1 = 1,
        ._resv2 = 1,
        .lba    = 1,
        .drive  = drive_number,
    };
    outb(ata_buses[bus_id].data_base + ATA_REGS_DRIVE_HEAD, drive_select.drive_head_register);
    ata_420ns_sleep(bus_id);
    return 0;
}

void ata_seek_lba(unsigned char bus_id, unsigned char drive_number, uint64_t lba) {
    kassert(bus_id < 2);
    kassert(drive_number < 2);
    if (!ata_buses[bus_id].is_initialized)               return;
    if (!ata_buses[bus_id].drives[drive_number].present) return;

    if (lba > ata_buses[bus_id].drives[drive_number].sector_count) {
        dkprintf("Warning: LBA seek over drive limit (%llu / %llu), ignoring request\n",
            lba, ata_buses[bus_id].drives[drive_number].sector_count);
        return;
    }

    ata_select_drive(bus_id, drive_number);

    if (ata_buses[bus_id].drives[drive_number].has_lba48) {
        struct ata_drive_head_register drive_select = {
            ._resv1 = 0, // ATA/ATAPI-6 spec says these bits are obsolete for lba48 and are supposed to be 0
            ._resv2 = 0,
            .lba    = 1,
            .drive  = drive_number,
        };
        outb(ata_buses[bus_id].data_base + ATA_REGS_DRIVE_HEAD, drive_select.drive_head_register);

        outb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_NUM, lba >> 24);
        outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_LOW, lba >> 32);
        outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_HIGH, lba >> 40);

        outb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_NUM, lba >> 0);
        outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_LOW, lba >> 8);
        outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_HIGH, lba >> 16);
        return;
    }

    if (ata_buses[bus_id].drives[drive_number].has_lba) {
        struct ata_drive_head_register drive_select = {
            ._resv1 = 1,
            ._resv2 = 1,
            .lba    = 1,
            .drive  = drive_number,
            .head   = lba >> 24 & 0b1111,
        };
        outb(ata_buses[bus_id].data_base + ATA_REGS_DRIVE_HEAD, drive_select.drive_head_register);

        outb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_NUM, lba >> 0);
        outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_LOW, lba >> 8);
        outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_HIGH, lba >> 16);
        return;
    }

    // CHS, conversion ripped from https://en.wikipedia.org/wiki/Logical_block_addressing#CHS_conversion
    unsigned short cylinder = lba /
        (ata_buses[bus_id].drives[drive_number].identify_block->current_head_count *
        ata_buses[bus_id].drives[drive_number].identify_block->current_sectors_per_track);
    unsigned short head =
        (lba / ata_buses[bus_id].drives[drive_number].identify_block->current_sectors_per_track) %
            ata_buses[bus_id].drives[drive_number].identify_block->current_head_count;
    unsigned short sector =
        (lba % ata_buses[bus_id].drives[drive_number].identify_block->current_sectors_per_track) + 1;

    struct ata_drive_head_register drive_select = {
        ._resv1 = 1,
        ._resv2 = 1,
        .lba    = 1,
        .drive  = drive_number,
        .head   = head & 0b1111,
    };
    outb(ata_buses[bus_id].data_base + ATA_REGS_DRIVE_HEAD, drive_select.drive_head_register);

    outb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_NUM, sector);
    outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_LOW, cylinder);
    outb(ata_buses[bus_id].data_base + ATA_REGS_CYLINDER_HIGH, cylinder >> 8);
}

#include "kernel_interrupts.h"
    void ata_irq_handler(unsigned char irq) {
    if (irq == PIC_INTERR_PRIMARY_ATA) {
        thread_queue_unblock_nonreentrant(&ata_buses[0].drive_queue);
    }
    else if (irq == PIC_INTERR_SECONDARY_ATA) {
        thread_queue_unblock_nonreentrant(&ata_buses[1].drive_queue);
    }
}

char ata_init_port(
    uint16_t primary_base, uint16_t primary_control_base,
    uint16_t secondary_base, uint16_t secondary_control_base
) {
    dkprintf("Enumerating primary %.4hx secondary %.4hx\n", primary_base, secondary_base);
    char found_bus = 0;
    ata_buses[0].is_initialized = 1;
    ata_buses[1].is_initialized = 1;

    ata_buses[0].data_base = primary_base;
    ata_buses[0].control_base = primary_control_base;
    ata_buses[0].drives[0].present = ata_buses[0].drives[1].present = 1;

    ata_buses[1].data_base = secondary_base;
    ata_buses[1].control_base = secondary_control_base;
    ata_buses[1].drives[0].present = ata_buses[1].drives[1].present = 1;

    if (inb(primary_base + ATA_REGS_STATUS) == 0xFF) {
        ata_buses[0].is_initialized = 0;
        dkprintf("Floating primary bus\n");
    } else found_bus = 1;

    if (inb(secondary_base + ATA_REGS_STATUS) == 0xFF) {
        ata_buses[1].is_initialized = 0;
        dkprintf("Floating secondary bus\n");
    } else found_bus = 1;

    if (!found_bus) {
        dkprintf("No drives on either bus (both floating), giving up...\n");
        return 0;
    }

    if (ata_buses[0].is_initialized) {
        ata_identify(0, 0);

        ata_identify(0, 1);
    }
    if (ata_buses[1].is_initialized) {
        ata_identify(1, 0);

        ata_identify(1, 1);
    }
    return 1;
}