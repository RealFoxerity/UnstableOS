#ifndef BLOCK_ATA_H
#define BLOCK_ATA_H

#include <stdint.h>

#define ATA_LEGACY_P_BASE 0x1F0
#define ATA_LEGACY_P_C_BASE 0x3F6

#define ATA_LEGACY_S_BASE 0x170
#define ATA_LEGACY_S_C_BASE 0x376

enum ata_base_registers {
    ATA_REGS_DATA,
    ATA_REGS_ERROR    = 1, // R
    ATA_REGS_FEATURES = 1, // W
    ATA_REGS_SECTOR_COUNT,
    ATA_REGS_SECTOR_NUM,    // or LBA low
    ATA_REGS_CYLINDER_LOW,  // or LBA med
    ATA_REGS_CYLINDER_HIGH, // or LBA high
    ATA_REGS_DRIVE_HEAD,
    ATA_REGS_STATUS  = 7, // R
    ATA_REGS_COMMAND = 7, // W
};

enum ata_control_registers {
    ATA_CREGS_ALTERNATE_STATUS = 0, // R
    ATA_CREGS_DEVICE_CONTROL   = 0, // W
    ATA_CREGS_DRIVE_ADDRESS
};

struct ata_device_control_register {
    union {
        struct {
            uint8_t _resv1 : 1; // always 0
            uint8_t interrupt_disable : 1;
            uint8_t software_reset : 1;
            uint8_t _resv2 : 1; // always 1
        };
        uint8_t device_control_register;
    };
};

struct ata_drive_head_register {
    union {
        struct {
            uint8_t head   : 4; // or LBA bits 24-27
            uint8_t drive  : 1; // 1 = slave
            uint8_t _resv1 : 1; // always 1
            uint8_t lba    : 1;
            uint8_t _resv2 : 1; // always 1
        };
        uint8_t drive_head_register;
    };
};

struct ata_drive_address_register {
    union {
        struct {
            uint8_t drive0_unselected       : 1;
            uint8_t drive1_unselected       : 1;
            uint8_t current_head_complement : 4;
            uint8_t drive_not_writing       : 1;
        };
        uint8_t drive_address_register;
    };
};

struct ata_status_register {
    union {
        struct {
            uint8_t error               : 1;
            uint8_t index               : 1;
            uint8_t corrected_data      : 1;
            uint8_t data_request        : 1;
            uint8_t drive_seek_complete : 1;
            uint8_t drive_fault         : 1;
            uint8_t drive_ready         : 1; // spun up among other things
            uint8_t busy                : 1;
        };
        uint8_t status_register;
    };
};

struct ata_error_register {
    union {
        struct {
            uint8_t address_mark_not_found : 1;
            uint8_t track_zero_not_found   : 1;
            uint8_t aborted                : 1;
            uint8_t media_change_request   : 1;
            uint8_t id_not_found           : 1;
            uint8_t media_changed          : 1;
            uint8_t unc_data_error         : 1;
            uint8_t bad_block              : 1;
        };
        uint8_t error_register;
    };
};

struct ata_drive {
    unsigned char present   : 1;
    unsigned char has_lba   : 1;
    unsigned char has_lba48 : 1;
    unsigned char has_dma   : 1;
    //unsigned char is_atapi  : 1;
    unsigned char ata_version;
    unsigned int sector_size;
    uint64_t sector_count;
    struct ata_identify * identify_block;
};

#include "kernel_sched.h"
struct ata_bus {
    char is_initialized;
    unsigned char current_drive : 1; // 0 = current drive is master, 1 = slave
    //spinlock_t drive_spinlock[2];
    thread_queue_t drive_queue;
    uint16_t data_base;
    uint16_t control_base;
    spinlock_t bus_lock; // set in the individual access modes, so far just the PIO
    struct ata_drive drives[2];
};

extern struct ata_bus ata_buses[2];

char ata_init_port(
    uint16_t primary_base, uint16_t primary_control_base,
    uint16_t secondary_base, uint16_t secondary_control_base);
void ata_irq_handler(unsigned char irq);
// probably don't use outside ata_send_command to be safe
void ata_bus_soft_reset(unsigned char bus_id);


/* return values for ata_send_command, don't use without bus lock!
 * -1 - device does not exist or disappeared
 * 0  - error
 * 1  - ok
 * 2  - ok, but corrected data
 */
char ata_send_command(unsigned char bus_id, unsigned char drive_number, uint8_t command);

/* return values for ata_select_drive, don't use without bus lock!
 * -1 - device does not exist
 * 0  - ok
 */
char ata_select_drive(unsigned char bus_id, unsigned char drive_number);
void ata_seek_lba(unsigned char bus_id, unsigned char drive_number, uint64_t lba);

// reads a pending block on the data port, don't use without bus lock!
void ata_read_pending_block(unsigned char bus_id, void * buf, unsigned int block_size);
// writes a block into the data port, don't use without bus lock!
void ata_write_pending_block(unsigned char bus_id, const void * buf, unsigned int block_size);

// read an entire sector using PIO mode
/* return values for ata_read
 * -1 - device does not exist or disappeared
 * 0  - error
 * 1  - ok
 * 2  - ok, but corrected data
 */
char ata_read(unsigned char bus_id, unsigned char drive_number, uint64_t lba, void * buf);

// writes an entire sector using PIO mode
/* return values for ata_write
 * -1 - device does not exist or disappeared
 * 0  - error
 * 1  - ok
 * 2  - ok, but corrected data
 */
char ata_write(unsigned char bus_id, unsigned char drive_number, uint64_t lba, const void * buf);
#endif