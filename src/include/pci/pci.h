#ifndef PCI_H
#define PCI_H

#define PCI_CS1_CONFIG_ADDRESS 0xCF8
#define PCI_CS1_CONFIG_DATA    0xCFC


#define PCI_BIOS_MAGIC 0x20494350 // _ICP
#define PCI_BIOS_CS1_SUPPORTED_MASK 1
#include <stdint.h>
#include <stddef.h>
struct pci_status {
    uint16_t                           : 2; // reserved
    uint16_t interrupt_state           : 1; // ro, see pci_command.interrupt_disable
    uint16_t capabilities_list         : 1; // ro, offset 0x34 is a pointer to a capabilities linked list
    uint16_t capable_66mhz             : 1; // ro
    uint16_t                           : 1; // reserved, was user definable features supported in PCI 2.1
    uint16_t fast_back_to_back_capable : 1; // ro

    /* if bus agent asserted PERR# on read or got one on write
     * and acted as the bus master
     * and pci_command.default_perr_action is set
     */
    uint16_t master_data_perr  : 1; // rw1c
    uint16_t devsel_timing     : 2; // ro, 0 = fast, 1 = medium, 2 = slow
    uint16_t sent_target_abort : 1; // ro
    uint16_t recv_target_abort : 1; // rw1c
    uint16_t recv_master_abort : 1; // rw1c
    uint16_t sent_serr         : 1; // rw1c, system error
    uint16_t detected_perr     : 1;
} __attribute__((packed));

struct pci_command {
    uint16_t io_space                : 1; // rw
    uint16_t mem_space               : 1; // rw
    uint16_t bus_master              : 1; // rw
    uint16_t special_cycle_monitor   : 1; // ro
    uint16_t write_invalidate_enable : 1; // ro
    uint16_t vga_palette_snoop       : 1; // ro

    /* if set, normal handling/reporting of PERR#
     * if not set, device will set bit 15 of status register, will not assert PERR#, and will continue operation
     */
    uint16_t default_perr_action     : 1; // rw

    uint16_t                         : 1; // reserved
    uint16_t serr_enable             : 1; // rw
    uint16_t fast_back_to_back_enable: 1; // ro
    uint16_t interrupt_disable       : 1; // rw
} __attribute__((packed));

struct pci_common_header {
    uint16_t vendor_id;
    uint16_t device_id;
    union {
        uint16_t command_word;
        struct pci_command command;
    };
    union {
        uint16_t status_word;
        struct pci_status status;
    };
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class;
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
} __attribute__((packed));

struct pci_pci_to_pci_bridge {
    struct pci_common_header _;
    uint32_t bar0;
    uint32_t bar1;
    uint8_t     primary_bus_number;
    uint8_t   secondary_bus_number;
    uint8_t subordinate_bus_number;
    uint8_t secondary_latency_timer;
    uint8_t io_base;
    uint8_t io_limit;
    uint16_t secondary_status;
    uint16_t memory_base;
    uint16_t memory_limit;
    uint16_t prefetchable_memory_base;
    uint16_t prefetchable_memory_limit;
    uint32_t prefetchable_memory_base_high;
    uint32_t prefetchable_memory_limit_high;
    uint16_t io_base_high;
    uint16_t io_limit_high;
    uint8_t capability_pointer;
    uint8_t __resv[3];
    uint32_t expansion_rom_base;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint16_t bridge_control;
} __attribute__((packed));

struct pci_general {
    struct pci_common_header _;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    uint32_t cardbus_cis_pointer;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t expansion_rom_base;
    uint8_t capability_pointer;
    uint8_t __resv[7];
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint8_t min_grant;
    uint8_t max_latency;
};


struct pci_bar {
    char is_io;
    char is_prefetchable;
    union {
        void * base_address;
        uint32_t io_address;
    };
    uint32_t size;
};

#define PCI_HEADER_MULTIFUNCTION  0x80
#define PCI_HEADER_GENERAL        0x00
#define PCI_HEADER_PCI_BRIDGE     0x01
#define PCI_HEADER_CARDBUS_BRIDGE 0x02

uint8_t  pci_read_8 (unsigned char bus, unsigned char device, unsigned char function, unsigned char offset);
uint16_t pci_read_16(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset);
uint32_t pci_read_32(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset);

void pci_write_8 (unsigned char bus, unsigned char device, unsigned char function, unsigned char offset, uint8_t  value);
void pci_write_16(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset, uint16_t value);
void pci_write_32(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset, uint32_t value);

void pci_enable_mem (uint8_t bus, uint8_t device, uint8_t function);
void pci_disable_mem(uint8_t bus, uint8_t device, uint8_t function);

struct pci_bar pci_get_bar(uint8_t bus, uint8_t device, uint8_t function, unsigned char bar_number);
void * pci_map_mmio(void * phys_start, size_t size, unsigned int page_flags);
uint16_t pci_get_ioport(unsigned int port_block_size);

struct pci_device {
    uint8_t bus, device, function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;
};

struct pci_driver {
    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class, subclass;

    // >= 0 success, < 0 error
    char (*init)(struct pci_device);
};
extern const struct pci_driver pci_drivers[];
extern const struct pci_driver pci_generic_drivers[];

void pci_init();
#endif