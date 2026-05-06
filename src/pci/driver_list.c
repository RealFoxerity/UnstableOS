#include <stddef.h>
#include <stdint.h>
#include "pci/pci.h"

extern char bga_init(struct pci_device);
const struct pci_driver pci_drivers[] = {
    { // bochs graphics adapter
        .vendor_id = 0x1234,
        .device_id = 0x1111,
        .init = bga_init,
    },
    {.init = NULL} // guarding NULL
};