#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Structure defining a single 8-byte GDT entry
struct gdt_entry_struct {
    uint16_t limit_low;           // The lower 16 bits of the limit
    uint16_t base_low;            // The lower 16 bits of the base
    uint8_t  base_middle;         // The next 8 bits of the base
    uint8_t  access;              // Access flags (Ring level, privilege, type)
    uint8_t  granularity;         // Size scaling and alignment flags
    uint8_t  base_high;           // The last 8 bits of the base
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

// Structure describing the GDT pointer passed directly to the CPU
struct gdt_ptr_struct {
    uint16_t limit;               // Total size of all GDT entries minus 1
    uint32_t base;                // The memory address of our GDT array
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

// Function to initialize the table layout
void init_gdt();

#endif