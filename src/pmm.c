#include "pmm.h"
#include "debug.h"
#include "serial.h"

// 131,072 bits can track 131,072 page frames.
// 131,072 * 4096 bytes = 512 Megabytes of total manageable physical RAM.
#define BITMAP_MAX_WORDS (131072 / 32)
#define PMM_MAX_MEMORY_BYTES ((uint64_t)BITMAP_MAX_WORDS * 32 * PMM_PAGE_SIZE)

static uint32_t pmm_bitmap[BITMAP_MAX_WORDS];
static uint32_t total_frames = 0;
static uint32_t used_frames = 0;

static uint32_t clamp_region_size(uint64_t base_addr, uint64_t size) {
    if (base_addr >= PMM_MAX_MEMORY_BYTES) {
        return 0;
    }

    uint64_t end_addr = base_addr + size;
    if (end_addr > PMM_MAX_MEMORY_BYTES) {
        end_addr = PMM_MAX_MEMORY_BYTES;
    }

    if (end_addr <= base_addr) {
        return 0;
    }

    return (uint32_t)(end_addr - base_addr);
}

// Inline bit manipulation logic utilities
static inline void bitmap_set(uint32_t frame) {
    pmm_bitmap[frame / 32] |= (1 << (frame % 32));
}

static inline void bitmap_clear(uint32_t frame) {
    pmm_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

static inline int bitmap_test(uint32_t frame) {
    return (pmm_bitmap[frame / 32] & (1 << (frame % 32)));
}

// Locate the first clear available bit ('0') inside our PMM tracker array
static int bitmap_first_free() {
    for (uint32_t i = 0; i < (total_frames / 32); i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) { // Optimization loop bypass
            for (int j = 0; j < 32; j++) {
                uint32_t bit = 1 << j;
                if (!(pmm_bitmap[i] & bit)) {
                    return (i * 32) + j;
                }
            }
        }
    }
    return -1;
}

void pmm_init_region(uint32_t base_addr, uint32_t size) {
    uint32_t align_addr = base_addr / PMM_PAGE_SIZE;
    uint32_t blocks = size / PMM_PAGE_SIZE;

    for (; blocks > 0; blocks--) {
        bitmap_clear(align_addr);
        align_addr++;
        used_frames--;
    }
}

void pmm_deinit_region(uint32_t base_addr, uint32_t size) {
    uint32_t align_addr = base_addr / PMM_PAGE_SIZE;
    uint32_t blocks = size / PMM_PAGE_SIZE;

    for (; blocks > 0; blocks--) {
        bitmap_set(align_addr);
        align_addr++;
        used_frames++;
    }
}

void pmm_init(const bios_e820_entry_t* mmap_entries, uint32_t mmap_entry_count,
              uint32_t kernel_start_addr, uint32_t kernel_end_addr) {
    // 1. Initially set the entire bitmap array to locked ('1') state
    for (int i = 0; i < BITMAP_MAX_WORDS; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }

    uint64_t highest_address = 0;
    kdebug_write("[ PMM ] E820 entries available for PMM scan: ");
    kdebug_write_dec(mmap_entry_count);
    kdebug_put('\n');

    for (uint32_t i = 0; i < mmap_entry_count; i++) {
        kdebug_write("[ PMM ] Entry ");
        kdebug_write_dec(i);
        kdebug_write(": base=");
        kdebug_write_hex((uint32_t)mmap_entries[i].base_addr);
        kdebug_write(" length=");
        kdebug_write_hex((uint32_t)mmap_entries[i].length);
        kdebug_write(" type=");
        kdebug_write_dec(mmap_entries[i].type);
        kdebug_put('\n');

        uint64_t region_end = mmap_entries[i].base_addr + mmap_entries[i].length;
        if (region_end > highest_address) {
            highest_address = region_end;
        }
    }

    if (highest_address > PMM_MAX_MEMORY_BYTES) {
        highest_address = PMM_MAX_MEMORY_BYTES;
    }

    total_frames = (uint32_t)(highest_address / PMM_PAGE_SIZE);
    used_frames = total_frames;

    kdebug_write("[ PMM ] Highest tracked physical address: ");
    kdebug_write_hex((uint32_t)highest_address);
    kdebug_put('\n');
    kdebug_write("[ PMM ] Total frame count after clamp: ");
    kdebug_write_dec(total_frames);
    kdebug_put('\n');

    if (total_frames == 0) {
        serial_write("[ PMM ] No usable E820 memory map entries were reported.\n");
        return;
    }

    for (uint32_t i = 0; i < mmap_entry_count; i++) {
        if (mmap_entries[i].type != BIOS_E820_MEMORY_AVAILABLE) {
            continue;
        }

        uint32_t region_size =
            clamp_region_size(mmap_entries[i].base_addr, mmap_entries[i].length);
        if (region_size == 0) {
            continue;
        }

        kdebug_write("[ PMM ] Marking region free: base=");
        kdebug_write_hex((uint32_t)mmap_entries[i].base_addr);
        kdebug_write(" size=");
        kdebug_write_hex(region_size);
        kdebug_put('\n');
        pmm_init_region((uint32_t)mmap_entries[i].base_addr, region_size);
    }

    // 2. Protect memory below 1MB (BIOS text zones, stack entry states)
    pmm_deinit_region(0x00, 0x100000);
    kdebug_write_line("[ PMM ] Reserved low memory region 0x00000000-0x000FFFFF.");

    // 3. Shield our compiled kernel memory footprint from allocator access
    if (kernel_end_addr > kernel_start_addr) {
        pmm_deinit_region(kernel_start_addr, kernel_end_addr - kernel_start_addr);
        kdebug_write("[ PMM ] Reserved kernel image region ");
        kdebug_write_hex(kernel_start_addr);
        kdebug_write(" - ");
        kdebug_write_hex(kernel_end_addr);
        kdebug_put('\n');
    }

    kdebug_write("[ PMM ] Used frames after reservation pass: ");
    kdebug_write_dec(used_frames);
    kdebug_put('\n');
    kdebug_write("[ PMM ] Free frames after reservation pass: ");
    kdebug_write_dec(total_frames - used_frames);
    kdebug_put('\n');
}

void* pmm_alloc_frame() {
    int free_frame_index = bitmap_first_free();
    if (free_frame_index == -1) {
        serial_write("[ PANIC ] System out of physical memory allocator frames!\n");
        return 0; // Kernel Allocation Failure
    }

    bitmap_set(free_frame_index);
    used_frames++;
    
    return (void*)(free_frame_index * PMM_PAGE_SIZE);
}

void pmm_free_frame(void* frame_addr) {
    uint32_t target_addr = (uint32_t)frame_addr;
    uint32_t frame_index = target_addr / PMM_PAGE_SIZE;

    bitmap_clear(frame_index);
    used_frames--;
}

uint32_t pmm_get_total_frames() { return total_frames; }
uint32_t pmm_get_used_frames() { return used_frames; }
