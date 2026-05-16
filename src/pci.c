#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000 |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000 |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_info_t *info) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t id = pci_config_read_dword((uint8_t)bus, slot, function, 0x00);
                if (id == 0xFFFFFFFF) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                if ((id & 0xFFFF) == vendor_id && ((id >> 16) & 0xFFFF) == device_id) {
                    uint32_t class_reg = pci_config_read_dword((uint8_t)bus, slot, function, 0x08);
                    uint32_t irq_reg = pci_config_read_dword((uint8_t)bus, slot, function, 0x3C);
                    uint32_t bar0 = pci_config_read_dword((uint8_t)bus, slot, function, 0x10);

                    if (info) {
                        info->bus = (uint8_t)bus;
                        info->slot = slot;
                        info->function = function;
                        info->vendor_id = vendor_id;
                        info->device_id = device_id;
                        info->class_code = (class_reg >> 24) & 0xFF;
                        info->subclass = (class_reg >> 16) & 0xFF;
                        info->prog_if = (class_reg >> 8) & 0xFF;
                        info->irq_line = irq_reg & 0xFF;
                        info->bar0 = bar0;
                    }
                    return 1;
                }
            }
        }
    }
    return 0;
}
