#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "memory_map.h"

#define PMM_PAGE_SIZE 4096

// Core memory manipulation API
void pmm_init(const bios_e820_entry_t* mmap_entries, uint32_t mmap_entry_count,
              uint32_t kernel_start_addr, uint32_t kernel_end_addr);
void pmm_init_region(uint32_t base_addr, uint32_t size);
void pmm_deinit_region(uint32_t base_addr, uint32_t size);

void* pmm_alloc_frame();
void pmm_free_frame(void* frame_addr);

// Debugging metric queries
uint32_t pmm_get_total_frames();
uint32_t pmm_get_used_frames();

#endif
