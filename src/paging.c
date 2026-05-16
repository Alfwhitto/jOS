#include "paging.h"
#include "debug.h"
#include "idt.h"
#include "monitor.h"
#include "pmm.h"

#define PAGE_COUNT 1024
#define MAX_DYNAMIC_PAGE_TABLES 16

static uint32_t page_directory[PAGE_COUNT] __attribute__((aligned(PAGING_PAGE_SIZE)));
static uint32_t first_page_table[PAGE_COUNT] __attribute__((aligned(PAGING_PAGE_SIZE)));
static uint32_t extra_page_tables[MAX_DYNAMIC_PAGE_TABLES][PAGE_COUNT]
    __attribute__((aligned(PAGING_PAGE_SIZE)));
static uint8_t extra_page_table_used[MAX_DYNAMIC_PAGE_TABLES];
static uint32_t last_kernel_end = 0;

static inline uint32_t read_cr0(void) {
    uint32_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline uint32_t read_cr2(void) {
    uint32_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline uint32_t read_cr3(void) {
    uint32_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void invalidate_page(uint32_t virtual_addr) {
    __asm__ volatile("invlpg (%0)" : : "r"((void*)virtual_addr) : "memory");
}

static inline void load_page_directory(uint32_t *directory) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(directory) : "memory");
}

static inline void enable_paging_bit(void) {
    uint32_t cr0 = read_cr0();
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static uint32_t* get_page_table(uint32_t directory_index) {
    if (directory_index == 0) {
        return first_page_table;
    }

    uint32_t entry = page_directory[directory_index];
    if (!(entry & PAGING_FLAG_PRESENT)) {
        return 0;
    }

    return (uint32_t *)(entry & 0xFFFFF000);
}

static uint32_t* allocate_page_table(uint32_t directory_index, uint32_t flags) {
    if (directory_index == 0) {
        return first_page_table;
    }

    for (uint32_t i = 0; i < MAX_DYNAMIC_PAGE_TABLES; i++) {
        if (!extra_page_table_used[i]) {
            extra_page_table_used[i] = 1;

            for (uint32_t j = 0; j < PAGE_COUNT; j++) {
                extra_page_tables[i][j] = 0;
            }

            page_directory[directory_index] =
                ((uint32_t)extra_page_tables[i]) | (flags & 0xFFF) | PAGING_FLAG_PRESENT;

            kdebug_write("    [map] Allocated page table for PDE ");
            kdebug_write_dec(directory_index);
            kdebug_write(" at ");
            kdebug_write_hex((uint32_t)extra_page_tables[i]);
            kdebug_put('\n');
            return extra_page_tables[i];
        }
    }

    klog_error("Out of bootstrap page tables while creating a new mapping.");
    return 0;
}

static void page_fault_handler(registers_t *regs) {
    uint32_t fault_address = read_cr2();

    monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    monitor_clear();

    kdebug_write_line("!!! PAGE FAULT PANIC !!!");
    kdebug_write("Faulting virtual address: ");
    kdebug_write_hex(fault_address);
    kdebug_put('\n');

    kdebug_write("Error code: ");
    kdebug_write_hex(regs->err_code);
    kdebug_put('\n');

    kdebug_write("Present violation: ");
    kdebug_write_line((regs->err_code & 0x1) ? "yes" : "no");
    kdebug_write("Write access: ");
    kdebug_write_line((regs->err_code & 0x2) ? "yes" : "no");
    kdebug_write("User-mode access: ");
    kdebug_write_line((regs->err_code & 0x4) ? "yes" : "no");
    kdebug_write("Reserved-bit overwrite: ");
    kdebug_write_line((regs->err_code & 0x8) ? "yes" : "no");
    kdebug_write("Instruction fetch: ");
    kdebug_write_line((regs->err_code & 0x10) ? "yes" : "no");
    kdebug_write("EIP at fault time: ");
    kdebug_write_hex(regs->eip);
    kdebug_put('\n');
    kdebug_write_line("System halted after page fault.");

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

void init_paging(uint32_t kernel_end_addr) {
    klog_info("Initializing paging subsystem...");
    last_kernel_end = kernel_end_addr;

    for (uint32_t i = 0; i < PAGE_COUNT; i++) {
        page_directory[i] = 0;
        first_page_table[i] = (i * PAGING_PAGE_SIZE) | PAGING_FLAG_PRESENT | PAGING_FLAG_WRITABLE;
    }

    for (uint32_t i = 0; i < MAX_DYNAMIC_PAGE_TABLES; i++) {
        extra_page_table_used[i] = 0;
        for (uint32_t j = 0; j < PAGE_COUNT; j++) {
            extra_page_tables[i][j] = 0;
        }
    }

    page_directory[0] = ((uint32_t)first_page_table) | PAGING_FLAG_PRESENT | PAGING_FLAG_WRITABLE;

    kdebug_write("    [map] Identity-mapped first 4 MiB. Kernel end at ");
    kdebug_write_hex(kernel_end_addr);
    kdebug_put('\n');
    kdebug_write("    [map] Page directory at ");
    kdebug_write_hex((uint32_t)page_directory);
    kdebug_write(", first table at ");
    kdebug_write_hex((uint32_t)first_page_table);
    kdebug_put('\n');
    kdebug_write("    [map] Dynamic page table pool capacity: ");
    kdebug_write_dec(MAX_DYNAMIC_PAGE_TABLES);
    kdebug_write_line(" tables.");

    register_interrupt_handler(14, page_fault_handler);
    kdebug_write_line("    [idt] Page fault handler registered on vector 14.");

    load_page_directory(page_directory);
    kdebug_write("    [cr3] Loaded page directory base ");
    kdebug_write_hex(read_cr3());
    kdebug_put('\n');

    kdebug_write("    [cr0] Value before paging enable ");
    kdebug_write_hex(read_cr0());
    kdebug_put('\n');

    enable_paging_bit();

    kdebug_write("    [cr0] Value after paging enable  ");
    kdebug_write_hex(read_cr0());
    kdebug_put('\n');
    klog_ok("Paging enabled. Identity mapping for the first 4 MiB is live.");
}

int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t page_aligned_virtual = virtual_addr & 0xFFFFF000;
    uint32_t page_aligned_physical = physical_addr & 0xFFFFF000;
    uint32_t directory_index = page_aligned_virtual >> 22;
    uint32_t table_index = (page_aligned_virtual >> 12) & 0x3FF;

    uint32_t *table = get_page_table(directory_index);
    if (table == 0) {
        table = allocate_page_table(directory_index, flags | PAGING_FLAG_WRITABLE);
        if (table == 0) {
            return 0;
        }
    }

    table[table_index] = page_aligned_physical | (flags & 0xFFF) | PAGING_FLAG_PRESENT;
    invalidate_page(page_aligned_virtual);

    kdebug_write("    [map] Mapped VA ");
    kdebug_write_hex(page_aligned_virtual);
    kdebug_write(" -> PA ");
    kdebug_write_hex(page_aligned_physical);
    kdebug_write(" flags ");
    kdebug_write_hex((flags & 0xFFF) | PAGING_FLAG_PRESENT);
    kdebug_put('\n');
    return 1;
}

int alloc_page(uint32_t virtual_addr, uint32_t flags) {
    void *frame = pmm_alloc_frame();
    if (frame == 0) {
        klog_error("alloc_page() failed: PMM returned no free physical frame.");
        return 0;
    }

    if (!map_page(virtual_addr, (uint32_t)frame, flags)) {
        pmm_free_frame(frame);
        return 0;
    }

    return 1;
}

int unmap_page(uint32_t virtual_addr, int free_physical_frame) {
    uint32_t page_aligned_virtual = virtual_addr & 0xFFFFF000;
    uint32_t directory_index = page_aligned_virtual >> 22;
    uint32_t table_index = (page_aligned_virtual >> 12) & 0x3FF;

    uint32_t *table = get_page_table(directory_index);
    if (table == 0 || !(table[table_index] & PAGING_FLAG_PRESENT)) {
        return 0;
    }

    uint32_t physical_addr = table[table_index] & 0xFFFFF000;
    table[table_index] = 0;
    invalidate_page(page_aligned_virtual);

    if (free_physical_frame) {
        pmm_free_frame((void*)physical_addr);
    }

    kdebug_write("    [map] Unmapped VA ");
    kdebug_write_hex(page_aligned_virtual);
    kdebug_write(" previously backed by PA ");
    kdebug_write_hex(physical_addr);
    kdebug_put('\n');
    return 1;
}

uint32_t virt_to_phys(uint32_t virtual_addr) {
    uint32_t directory_index = virtual_addr >> 22;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;
    uint32_t *table = get_page_table(directory_index);

    if (table == 0 || !(table[table_index] & PAGING_FLAG_PRESENT)) {
        return 0;
    }

    return (table[table_index] & 0xFFFFF000) | (virtual_addr & 0xFFF);
}

int is_page_mapped(uint32_t virtual_addr) {
    return virt_to_phys(virtual_addr) != 0;
}
