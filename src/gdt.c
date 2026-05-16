#include "gdt.h"

gdt_entry_t gdt_entries[3];
gdt_ptr_t   gdt_ptr;

// Declared in assembly (kernel_entry.asm) to load the pointer and far jump
extern void gdt_flush(uint32_t);

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

void init_gdt() {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 3) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    // Descriptor 0: Obligatory Null Segment
    gdt_set_gate(0, 0, 0, 0, 0);

    // Descriptor 1: Flat Code Segment (4GB, base 0, Ring 0 execution)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // Descriptor 2: Flat Data Segment (4GB, base 0, Ring 0 read/write)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    gdt_flush((uint32_t)&gdt_ptr);
}