#ifndef FAT_H
#define FAT_H

#include <stdint.h>

typedef struct fat_file_info {
    char name[13];
    uint32_t size;
    uint8_t is_directory;
} fat_file_info_t;

int fat_mount(uint8_t drive);
int fat_is_mounted(void);
int fat_list_root(fat_file_info_t *entries, uint32_t max_entries, uint32_t *entry_count);
int fat_read_file(const char *name, uint8_t **buffer, uint32_t *size_out);
int fat_write_file(const char *name, const uint8_t *data, uint32_t size);
int fat_delete_file(const char *name);

#endif
