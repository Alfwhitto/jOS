#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>
#include <stdint.h>

void init_kheap(uint32_t kernel_end_addr);
void* kmalloc(size_t size);
void kfree(void *ptr);

uint32_t kheap_get_total_bytes(void);
uint32_t kheap_get_free_bytes(void);
uint32_t kheap_get_used_bytes(void);
uint32_t kheap_get_largest_free_block(void);

#endif
