#include "pci/pci.h"
#include "lowlevel.h"

#include "kernel_spinlock.h"

#include <stdint.h>

spinlock_t pci_access_spinlock;

static uint32_t pci_get_address(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset) {
    uint32_t address = 0x80000000; // enable bit
    address |= (bus      & 0xFF   ) << 16;
    address |= (device   & 0b11111) << 11;
    address |= (function & 0b00111) << 8;
    address |=  offset   & 0xFC;
    return address;
}

uint8_t pci_read_8(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset) {
    spinlock_acquire(&pci_access_spinlock);
    outl(PCI_CS1_CONFIG_ADDRESS, pci_get_address(bus, device, function, offset));

    uint8_t ret = inb(PCI_CS1_CONFIG_DATA + (offset & 3));
    spinlock_release(&pci_access_spinlock);
    return ret;
}

uint16_t pci_read_16(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset) {
    spinlock_acquire(&pci_access_spinlock);
    outl(PCI_CS1_CONFIG_ADDRESS, pci_get_address(bus, device, function, offset));

    uint16_t ret = inw(PCI_CS1_CONFIG_DATA + (offset & 2));
    spinlock_release(&pci_access_spinlock);
    return ret;
}

uint32_t pci_read_32(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset) {
    spinlock_acquire(&pci_access_spinlock);
    outl(PCI_CS1_CONFIG_ADDRESS, pci_get_address(bus, device, function, offset));

    uint32_t ret = inl(PCI_CS1_CONFIG_DATA);
    spinlock_release(&pci_access_spinlock);
    return ret;
}

void pci_write_8(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset, uint8_t value) {
    spinlock_acquire(&pci_access_spinlock);
    outl(PCI_CS1_CONFIG_ADDRESS, pci_get_address(bus, device, function, offset));

    outb(PCI_CS1_CONFIG_DATA + (offset & 3), value);
    spinlock_release(&pci_access_spinlock);
}

void pci_write_16(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset, uint16_t value) {
    spinlock_acquire(&pci_access_spinlock);
    outl(PCI_CS1_CONFIG_ADDRESS, pci_get_address(bus, device, function, offset));

    outw(PCI_CS1_CONFIG_DATA + (offset & 2), value);
    spinlock_release(&pci_access_spinlock);
}

void pci_write_32(unsigned char bus, unsigned char device, unsigned char function, unsigned char offset, uint32_t value) {
    spinlock_acquire(&pci_access_spinlock);
    outl(PCI_CS1_CONFIG_ADDRESS, pci_get_address(bus, device, function, offset));

    outl(PCI_CS1_CONFIG_DATA, value);
    spinlock_release(&pci_access_spinlock);
}