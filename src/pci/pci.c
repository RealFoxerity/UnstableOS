#include "kernel.h"
#include "lowlevel.h"
#include "mm/kernel_memory.h"
#include "pci/pci.h"

#include "v8086.h"

#include <string.h>
#include <stddef.h>

static void pci_enumerate();

void pci_init() {
    v86_mcontext_t pci_bios_supported = v86_call_bios(
        0x1A, (v86_mcontext_t) {
            .eax = 0xB101, // PCI BIOS v2 installation
            .edi = 0
        }
    );
    if ((pci_bios_supported.eax & 0xFF00) != 0 || // pci bios installed
        pci_bios_supported.edx != PCI_BIOS_MAGIC  // function supported
    ) {
        // theoretically we could try probing ourselves, but probably not a good idea
        // considering even the bios32 spec came out in 1993, i think it's ok
        kprintf("PCI BIOS not supported, won't use PCI\n");
        return;
    }

    if (!(pci_bios_supported.eax & PCI_BIOS_CS1_SUPPORTED_MASK)) {
        kprintf("PCI doesn't suport configuration space 1, won't use PCI\n");
        return;
    }

    // edi holds the protected mode address of the PCI bios
    unsigned char last_pci_bus = pci_bios_supported.ecx;
    kprintf("Running PCI v%hhx.%hhx\n",
        (unsigned char)(pci_bios_supported.ebx >> 8 & 0xFF), (unsigned char)pci_bios_supported.ebx);

    pci_enumerate();
}

struct pci_bar_dword {
    union {
        struct {
            uint32_t is_io_1 : 1;
            uint32_t type  : 2;
            uint32_t prefetchable : 1;
            uint32_t address : 28;
        } __attribute__((packed));
        struct {
            uint32_t is_io_2   : 1;
            uint32_t : 1;
            uint32_t io_port : 30;
        } __attribute__((packed));
    };
};


void pci_enable_mem(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_read_16(
        bus, device, function,
        offsetof(struct pci_common_header, command_word)
    );
    command |= 7; // io and mem space access, bus mastering enabling
    pci_write_16(
        bus, device, function,
        offsetof(struct pci_common_header, command_word),
        command
    );
}

void pci_disable_mem(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_read_16(
        bus, device, function,
        offsetof(struct pci_common_header, command_word)
    );
    command &= 7; // io and mem space access, bus mastering enabling
    pci_write_16(
        bus, device, function,
        offsetof(struct pci_common_header, command_word),
        command
    );
}

struct pci_bar pci_get_bar(uint8_t bus, uint8_t device, uint8_t function, unsigned char bar_number) {
    // general device has 6 (0 - 5) bars
    // PCI-PCI bridge has 2 (0 - 1) bars
    // PCI-CardBus has zero, we don't support them anyway

    uint8_t type = pci_read_8(bus, device, function, offsetof(struct pci_common_header, header_type));
    type &= 0x7F; // get rid of the multifunction bit
    if (type == PCI_HEADER_GENERAL    && bar_number > 5) return (struct pci_bar){0};
    if (type == PCI_HEADER_PCI_BRIDGE && bar_number > 1) return (struct pci_bar){0};
    if (type == PCI_HEADER_CARDBUS_BRIDGE)               return (struct pci_bar){0};

    // get base info
    uint32_t bar_orig = pci_read_32(
        bus, device, function,
        offsetof(struct pci_general, bar0) + bar_number*sizeof(uint32_t)
    );

    struct pci_bar_dword bar_dword = *(struct pci_bar_dword *)&bar_orig;

    // either the now unsupported mode 1 for <1M 16 bit addresses
    // or 64 bit bar
    // the former of which we don't support and the latter we can't support in 32bit mode
    if (bar_dword.type != 0) return (struct pci_bar){0};

    struct pci_bar out = {0};

    if (bar_dword.is_io_1) {
        out.is_io = bar_dword.is_io_1;
        out.io_address = bar_dword.io_port << 2;
        return out; // size doesn't make sense for I/O ports
    }

    // get size
    // disable io/mem access in case all ones are interpreted as unintentional read
    pci_disable_mem(bus, device, function);

    // set bar to all 1s to get size
    pci_write_32(
        bus, device, function,
        offsetof(struct pci_general, bar0) + bar_number*sizeof(uint32_t),
        ~0
    );

    uint32_t bar_size = pci_read_32(
        bus, device, function,
        offsetof(struct pci_general, bar0) + bar_number*sizeof(uint32_t)
    );

    // restore original bar
    pci_write_32(
        bus, device, function,
        offsetof(struct pci_general, bar0) + bar_number*sizeof(uint32_t),
        bar_orig
    );

    pci_enable_mem(bus, device, function);

    out.is_io = bar_dword.is_io_2;
    out.is_prefetchable = bar_dword.prefetchable;
    out.base_address = (void*)(bar_dword.address << 4);
    out.size = ~(bar_size & ~15);
    return out;
}

void pci_set_bar(uint8_t bus, uint8_t device, uint8_t function, unsigned char bar_number, uint32_t base_address) {
    uint8_t type = pci_read_8(bus, device, function, offsetof(struct pci_common_header, header_type));
    type &= 0x7F; // get rid of the multifunction bit
    if (type == PCI_HEADER_GENERAL    && bar_number > 5) return;
    if (type == PCI_HEADER_PCI_BRIDGE && bar_number > 1) return;
    if (type == PCI_HEADER_CARDBUS_BRIDGE)               return;

    uint32_t bar_orig = pci_read_32(
        bus, device, function,
        offsetof(struct pci_general, bar0) + bar_number*sizeof(uint32_t)
    );

    struct pci_bar_dword bar_dword = *(struct pci_bar_dword *)&bar_orig;
    if (bar_dword.type != 0) return;

    if (bar_dword.is_io_1) {
        bar_dword.io_port = base_address >> 2;
    } else {
        bar_dword.address = base_address >> 4;
    }

    pci_write_32(
        bus, device, function,
        offsetof(struct pci_general, bar0) + bar_number*sizeof(uint32_t),
        *(uint32_t *)&bar_dword
    );
}

static struct pci_driver pci_get_driver(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; pci_drivers[i].init != NULL; i++) {
        if (pci_drivers[i].vendor_id == vendor_id && pci_drivers[i].device_id == device_id) {
            return pci_drivers[i];
        }
    }

    return (struct pci_driver){0};
}

// 1 if multifunction
static char pci_init_dev(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t device_id = pci_read_16(bus, device, function, offsetof(struct pci_common_header, device_id));
    if (device_id == 0xFFFF) return -1; // no such device exists

    uint16_t vendor_id = pci_read_16(bus, device, function, offsetof(struct pci_common_header, vendor_id));

    uint8_t type = pci_read_8(bus, device, function, offsetof(struct pci_common_header, header_type));

    uint8_t class = pci_read_8(bus, device, function, offsetof(struct pci_common_header, class));
    uint8_t subclass = pci_read_8(bus, device, function, offsetof(struct pci_common_header, subclass));
    uint8_t prog_if = pci_read_8(bus, device, function, offsetof(struct pci_common_header, prog_if));

    uint8_t revision = pci_read_8(bus, device, function, offsetof(struct pci_common_header, revision_id));

    struct pci_device pci_device = {
        .bus = bus,
        .device = device,
        .function = function,
        .vendor_id = vendor_id,
        .device_id = device_id,
        .class = class,
        .subclass = subclass,
        .prog_if = prog_if
    };

    kprintf("%.2hhx:%.2hhx.%.2hhx %.2hhx%.2hhx: %.4hx:%.4hx (rev %u)",
        bus, device, function,
        class, subclass,
        vendor_id, device_id,
        revision
    );
    if ((type & 0x7F) == PCI_HEADER_GENERAL && class != 0x6 && class != 0xB) { // 6 is bridge, 0xB is CPU
        struct pci_driver driver = pci_get_driver(vendor_id, device_id);
        if (driver.init != NULL) {
            kprintf("\n");
            driver.init(pci_device);
        } else kprintf(" - No driver\n");
    } else {
        kprintf("\n");
    }
    if (type & PCI_HEADER_MULTIFUNCTION) return 1;
    return 0;
}

static void pci_enumerate_bus(int bus) {
    for (int device = 0; device < 32; device++) {
        if (!pci_init_dev(bus, device, 0)) continue;
        for (int func = 1; func < 8; func++) {
            pci_init_dev(bus, device, func);
        }
    }
}

static void pci_enumerate() {
    // root bus is all 0

    /*
    tested on some real hardware with PCI and PCIe buses (at once) and this method didn't work
    probably will work on PCI-only systems, I however don't have any such systems
    seems to work in QEMU, VirtualBox, and VMWare though
    if (pci_init_dev(0, 0, 0)) {
        for (int func = 0; func < 8; func ++) {
            if (pci_init_dev(0, 0, func) == -1) break;
            pci_enumerate_bus(func);
        }
    } else {
        pci_enumerate_bus(0);
    }
    */

    for (int bus = 0; bus < 256; bus ++) {
        pci_enumerate_bus(bus);
    }
}

static void * current_mmio_top = PCI_MMIO_START;
static spinlock_t mmio_spinlock = {0};
void * pci_map_mmio(void * phys_start, size_t size, unsigned int page_flags) {
    if (size == 0) return NULL;
    if (current_mmio_top + size > PCI_MMIO_END ||
        current_mmio_top + size < PCI_MMIO_START) return NULL;

    spinlock_acquire(&mmio_spinlock);
    if (current_mmio_top + size > PCI_MMIO_END) { // raced on mmio top
        spinlock_release(&mmio_spinlock);
        return NULL;
    }

    // align to page size
    if (size & (PAGE_SIZE_NO_PAE - 1)) {
        size &= PAGE_SIZE_NO_PAE - 1;
        size += PAGE_SIZE_NO_PAE;
    }

    for (size_t page = 0; page < size / PAGE_SIZE_NO_PAE; page++) {
        paging_map_phys_addr(phys_start,
            current_mmio_top + PAGE_SIZE_NO_PAE,
            page_flags
        );
    }
    void * old_mmio = current_mmio_top;
    current_mmio_top += size;

    spinlock_release(&mmio_spinlock);
    return old_mmio;
}