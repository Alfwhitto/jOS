#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGING_FLAG_PRESENT  0x1
#define PAGING_FLAG_WRITABLE 0x2
#define PAGING_FLAG_USER     0x4
#define PAGING_PAGE_SIZE     0x1000

void init_paging(uint32_t kernel_end_addr);
int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
int alloc_page(uint32_t virtual_addr, uint32_t flags);
int unmap_page(uint32_t virtual_addr, int free_physical_frame);
uint32_t virt_to_phys(uint32_t virtual_addr);
int is_page_mapped(uint32_t virtual_addr);

#endif
