#include "kheap.h"
#include "debug.h"
#include "monitor.h"
#include "paging.h"

#define KHEAP_ALIGNMENT 8
#define KHEAP_START        0x00400000
#define KHEAP_INITIAL_SIZE (64 * 1024)
#define KHEAP_GROWTH_SIZE  (64 * 1024)
#define KHEAP_VIRTUAL_LIMIT 0x04000000

typedef struct heap_block {
    uint32_t size;
    uint8_t free;
    struct heap_block *next;
} heap_block_t;

static heap_block_t *heap_head = 0;
static uint32_t heap_start = 0;
static uint32_t heap_end = 0;
static uint32_t heap_total = 0;

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void split_block(heap_block_t *block, uint32_t requested_size) {
    if (block->size <= requested_size + sizeof(heap_block_t) + KHEAP_ALIGNMENT) {
        return;
    }

    heap_block_t *new_block =
        (heap_block_t *)((uint32_t)block + sizeof(heap_block_t) + requested_size);
    new_block->size = block->size - requested_size - sizeof(heap_block_t);
    new_block->free = 1;
    new_block->next = block->next;

    block->size = requested_size;
    block->next = new_block;
}

static void coalesce_blocks(void) {
    heap_block_t *current = heap_head;

    while (current && current->next) {
        uint32_t current_end =
            (uint32_t)current + sizeof(heap_block_t) + current->size;

        if (current->free && current->next->free &&
            current_end == (uint32_t)current->next) {
            current->size += sizeof(heap_block_t) + current->next->size;
            current->next = current->next->next;
            continue;
        }

        current = current->next;
    }
}

static heap_block_t* find_last_block(void) {
    heap_block_t *current = heap_head;
    if (current == 0) {
        return 0;
    }

    while (current->next) {
        current = current->next;
    }

    return current;
}

static int map_heap_pages(uint32_t start_addr, uint32_t size) {
    for (uint32_t offset = 0; offset < size; offset += PAGING_PAGE_SIZE) {
        if (!alloc_page(start_addr + offset, PAGING_FLAG_WRITABLE)) {
            return 0;
        }
    }

    return 1;
}

static int grow_heap(uint32_t minimum_additional_bytes) {
    uint32_t growth_target = minimum_additional_bytes + sizeof(heap_block_t);
    uint32_t growth_bytes = KHEAP_GROWTH_SIZE;

    while (growth_bytes < growth_target) {
        growth_bytes += KHEAP_GROWTH_SIZE;
    }

    if (heap_end + growth_bytes > KHEAP_VIRTUAL_LIMIT) {
        klog_error("Kernel heap cannot grow further: virtual heap window exhausted.");
        return 0;
    }

    if (!map_heap_pages(heap_end, growth_bytes)) {
        klog_error("Kernel heap growth failed while mapping additional pages.");
        return 0;
    }

    heap_block_t *last = find_last_block();
    heap_block_t *new_block = (heap_block_t *)heap_end;
    new_block->size = growth_bytes - sizeof(heap_block_t);
    new_block->free = 1;
    new_block->next = 0;

    if (last == 0) {
        heap_head = new_block;
    } else if (last->free &&
               ((uint32_t)last + sizeof(heap_block_t) + last->size) == heap_end) {
        last->size += growth_bytes;
    } else {
        last->next = new_block;
    }

    heap_end += growth_bytes;
    heap_total += growth_bytes;
    coalesce_blocks();

    kdebug_write("    [heap] Grew heap by ");
    kdebug_write_dec(growth_bytes);
    kdebug_write(" bytes. New heap end: ");
    kdebug_write_hex(heap_end);
    kdebug_put('\n');
    return 1;
}

void init_kheap(uint32_t kernel_end_addr) {
    (void)kernel_end_addr;
    heap_start = KHEAP_START;
    heap_end = heap_start + KHEAP_INITIAL_SIZE;

    if (heap_end > KHEAP_VIRTUAL_LIMIT) {
        klog_error("Kernel heap initialization failed: identity-mapped window exhausted.");
        return;
    }

    if (!map_heap_pages(heap_start, KHEAP_INITIAL_SIZE)) {
        klog_error("Kernel heap initialization failed: unable to map heap bootstrap pages.");
        return;
    }

    heap_head = (heap_block_t *)heap_start;
    heap_head->size = KHEAP_INITIAL_SIZE - sizeof(heap_block_t);
    heap_head->free = 1;
    heap_head->next = 0;
    heap_total = heap_head->size;

    klog_info("Initializing kernel heap allocator...");
    kdebug_write("    [heap] Heap virtual start: ");
    kdebug_write_hex(heap_start);
    kdebug_put('\n');
    kdebug_write("    [heap] Heap virtual end  : ");
    kdebug_write_hex(heap_end);
    kdebug_put('\n');
    kdebug_write("    [heap] Initial usable bytes: ");
    kdebug_write_dec(heap_total);
    kdebug_put('\n');
    klog_ok("Kernel heap is online inside the identity-mapped bootstrap window.");
}

void* kmalloc(size_t size) {
    if (size == 0 || heap_head == 0) {
        return 0;
    }

    uint32_t requested_size = align_up((uint32_t)size, KHEAP_ALIGNMENT);
    heap_block_t *current = heap_head;

    while (current) {
        if (current->free && current->size >= requested_size) {
            split_block(current, requested_size);
            current->free = 0;
            return (void *)((uint32_t)current + sizeof(heap_block_t));
        }
        current = current->next;
    }

    if (!grow_heap(requested_size)) {
        klog_warn("kmalloc() could not satisfy a heap allocation request.");
        return 0;
    }

    return kmalloc(size);
}

void kfree(void *ptr) {
    if (ptr == 0) {
        return;
    }

    heap_block_t *block = (heap_block_t *)((uint32_t)ptr - sizeof(heap_block_t));
    block->free = 1;
    coalesce_blocks();
}

uint32_t kheap_get_total_bytes(void) {
    return heap_total;
}

uint32_t kheap_get_free_bytes(void) {
    uint32_t free_bytes = 0;
    heap_block_t *current = heap_head;

    while (current) {
        if (current->free) {
            free_bytes += current->size;
        }
        current = current->next;
    }

    return free_bytes;
}

uint32_t kheap_get_used_bytes(void) {
    return heap_total - kheap_get_free_bytes();
}

uint32_t kheap_get_largest_free_block(void) {
    uint32_t largest = 0;
    heap_block_t *current = heap_head;

    while (current) {
        if (current->free && current->size > largest) {
            largest = current->size;
        }
        current = current->next;
    }

    return largest;
}
