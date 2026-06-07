#include "kernel.h"
#include "lowlevel.h"
#include "block/ata/ata.h"
#include "block/ata/ata_commands.h"

#define dkprintf(format, ...) kprintf("ATA: "format, ##__VA_ARGS__)


const static struct timespec ata_spinup_timeout = {.tv_sec = 30};

// checking for ERR bit, 1msec * 30 attempts with each 10th attempt soft resetting the bus
// for context, 5 msec per command (assuming single block transfer) is 100 KiB/s, which is abysmally slow,
// that it's basically guaranteed that if the device is ok, the command will finish in time
const static struct timespec ata_command_timeout = {.tv_nsec = 1000000};
#define ATA_MAXIMUM_ATTEMPTS 30

/* return values for ata_send_command
 * -1 - device does not exist or disappeared
 * 0  - error
 * 1  - ok
 * 2  - ok, but corrected data
 */
// idea here is to not unblock IRQ until next command to not needlessly get them
// so we never read the status register, and we leave irq masked on exit
char ata_send_command(unsigned char bus_id, unsigned char drive_number, uint8_t command) {
    kassert(bus_id < 2);
    kassert(drive_number < 2);
    if (!ata_buses[bus_id].is_initialized)               return -1;
    if (!ata_buses[bus_id].drives[drive_number].present) return -1;

    int attempts = 0;
    again:
    if (attempts >= ATA_MAXIMUM_ATTEMPTS) {
        dkprintf("Command %.2hhx failed on bus %d drive %d\n", command, bus_id, drive_number);

        // unmask any potential IRQs to keep the device state consistent
        inw(ata_buses[bus_id].data_base + ATA_REGS_STATUS);
        return 0;
    }

    if (ata_select_drive(bus_id, drive_number) != 0) return 0;


    // it is possible that we get the IRQ before we add ourselves to the queue
    // since a side effect of calling the queue_add is that we reschedule (and thus enable interrupts)
    // we can momentarily turn them off here; TODO: fix when SMP
    asm volatile ("cli;");
    outb(ata_buses[bus_id].data_base + ATA_REGS_COMMAND, command);

    if (thread_queue_add_with_timeout(
        &ata_buses[bus_id].drive_queue,
        current_process, current_thread,
        ata_command_timeout) == 1) {
        asm volatile ("sti;");


        struct ata_status_register timeout_status =
            (struct ata_status_register) {
            .status_register = inb(ata_buses[bus_id].control_base + ATA_CREGS_ALTERNATE_STATUS)
        };
        if (timeout_status.status_register == 0) {
            dkprintf("No device controlling bus %d drive %d\n", bus_id, drive_number);
            kfree(ata_buses[bus_id].drives[drive_number].identify_block);
            ata_buses[bus_id].drives[drive_number].present = 0;
            return -1;
        }
        if (timeout_status.drive_fault) {
            dkprintf("Fault on command %.2hhx on bus %d drive %d, attempt %d\n", command, bus_id, drive_number, attempts + 1);
        } else if (timeout_status.error) {
            dkprintf("Error (%.2hhx) on command %.2hhx on bus %d drive %d, attempt %d\n",
                inb(ata_buses[bus_id].data_base + ATA_REGS_ERROR),
                command, bus_id, drive_number, attempts + 1);
        } else if (timeout_status.data_request || (command == ATA_FLUSH_CACHE && !timeout_status.data_request)) {
            //dkprintf("Warning: lost IRQ on command %.2hhx on bus %d drive %d, attempt %d?\n", command, bus_id, drive_number, attempts + 1);
            goto ok;
        } else if (timeout_status.drive_ready) {
            //dkprintf("Command %.2hhx lost on bus %d drive %d, attempt %d?\n", command, bus_id, drive_number, attempts + 1);
        } else {
            //dkprintf("Timeout on command %.2hhx on bus %d drive %d, attempt %d\n", command, bus_id, drive_number, attempts + 1);
        }

        attempts ++;

        // this would normally unmask the irq, however, sending commands and soft resetting does so as well
        //inb(ata_buses[bus_id].data_base + ATA_REGS_STATUS);

        if (attempts % 10 == 0)
            ata_bus_soft_reset(bus_id);
        goto again;
    }

    ok:
    asm volatile ("sti;");

    // reading normal status unmasks IRQ
    struct ata_status_register status =
        (struct ata_status_register) {
            .status_register = inb(ata_buses[bus_id].control_base + ATA_CREGS_ALTERNATE_STATUS)
        };

    //ATAPI-5 8.10 - FLUSH CACHE, "DRQ shall be cleared to zero."
    if ((status.busy || !status.data_request) &&
        !(command == ATA_FLUSH_CACHE && !status.data_request)) {

        // according to the ATA spec (or at least ATA-2 :P), the INTRQ line is asserted when:
        // per a ready block transmit in PIO (not on error, that's handled in the timeout),
        // or at the end of a DMA request
        //dkprintf("Unexpected interrupt on command %.2hhx on bus %d drive %d, attempt %d, status: %.2hhx\n",
        //    command, bus_id, drive_number, attempts + 1, status.status_register);
        attempts ++;

        ata_bus_soft_reset(bus_id);
        goto again;
    }
    if (status.corrected_data) {
        dkprintf("Warning: data ECC action on command %.2hhx on bus %d drive %d\n", command, bus_id, drive_number);
        return 2;
    }
    return 1;
}


/* return values for ata_read
 * -1 - device does not exist or disappeared
 * 0  - error
 * 1  - ok
 * 2  - ok, but corrected data
 */
static char __ata_read(unsigned char bus_id, unsigned char drive_number, uint64_t lba, void * buf) {
    if (ata_select_drive(bus_id, drive_number) != 0) return 0;

    ata_seek_lba(bus_id, drive_number, lba);

    outb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_COUNT, 1);

    char ret = -1;
    if (ata_buses[bus_id].drives[drive_number].has_lba48)
        ret = ata_send_command(bus_id, drive_number, ATA_READ_SECTORS_EXT);
    else
        ret = ata_send_command(bus_id, drive_number, ATA_READ_SECTORS);

    if (ret <= 0) return ret;

    if (ret == 2) {
        dkprintf("--- ECC on read LBA %lld on bus %d drive %d\n", lba, bus_id, drive_number);
    }

    ata_read_pending_block(bus_id, buf, ata_buses[bus_id].drives[drive_number].sector_size);
    return ret;
}

char ata_read(unsigned char bus_id, unsigned char drive_number, uint64_t lba, void * buf) {
    kassert(bus_id < 2);
    kassert(drive_number < 2);
    if (!ata_buses[bus_id].is_initialized)               return -1;
    if (!ata_buses[bus_id].drives[drive_number].present) return -1;

    spinlock_acquire(&ata_buses[bus_id].bus_lock);
    char ret = __ata_read(bus_id, drive_number, lba, buf);
    spinlock_release(&ata_buses[bus_id].bus_lock);
    return ret;
}

/* return values for ata_write
 * -1 - device does not exist or disappeared
 * 0  - error
 * 1  - ok
 * 2  - ok, but corrected data
 */
static char __ata_write(unsigned char bus_id, unsigned char drive_number, uint64_t lba, const void * buf) {
    if (ata_select_drive(bus_id, drive_number) != 0) return 0;

    ata_seek_lba(bus_id, drive_number, lba);

    outb(ata_buses[bus_id].data_base + ATA_REGS_SECTOR_COUNT, 1);

    char ret = -1;
    if (ata_buses[bus_id].drives[drive_number].has_lba48)
        ret = ata_send_command(bus_id, drive_number, ATA_WRITE_SECTORS_EXT);
    else
        ret = ata_send_command(bus_id, drive_number, ATA_WRITE_SECTORS);

    if (ret <= 0) return ret;

    if (ret == 2) {
        dkprintf("--- ECC on write LBA %lld on bus %d drive %d\n", lba, bus_id, drive_number);
    }

    ata_write_pending_block(bus_id, buf, ata_buses[bus_id].drives[drive_number].sector_size);

    if (ata_buses[bus_id].drives[drive_number].ata_version > 4)
        ata_send_command(bus_id, drive_number, ATA_FLUSH_CACHE);

    return ret;
}

char ata_write(unsigned char bus_id, unsigned char drive_number, uint64_t lba, const void * buf) {
    kassert(bus_id < 2);
    kassert(drive_number < 2);
    if (!ata_buses[bus_id].is_initialized)               return -1;
    if (!ata_buses[bus_id].drives[drive_number].present) return -1;

    spinlock_acquire(&ata_buses[bus_id].bus_lock);
    char ret = __ata_write(bus_id, drive_number, lba, buf);
    spinlock_release(&ata_buses[bus_id].bus_lock);
    return ret;
}