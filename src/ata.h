#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_DRIVE_MASTER 0
#define ATA_DRIVE_SLAVE  1

typedef struct ata_device_info {
    uint8_t present;
    uint8_t drive;
    uint32_t sector_count;
    char model[41];
} ata_device_info_t;

int ata_identify(uint8_t drive, ata_device_info_t *info);
int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, void *buffer);
int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, const void *buffer);

#endif
