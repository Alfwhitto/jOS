#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>

#define BIOS_E820_MEMORY_AVAILABLE 1

typedef struct bios_e820_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_attrs;
} __attribute__((packed)) bios_e820_entry_t;

#endif
