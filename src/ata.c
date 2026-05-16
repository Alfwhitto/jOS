#include "ata.h"
#include "io.h"

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6

#define ATA_REG_DATA        0x00
#define ATA_REG_SECCOUNT0   0x02
#define ATA_REG_LBA0        0x03
#define ATA_REG_LBA1        0x04
#define ATA_REG_LBA2        0x05
#define ATA_REG_HDDEVSEL    0x06
#define ATA_REG_COMMAND     0x07
#define ATA_REG_STATUS      0x07

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY    0xEC

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static void ata_io_wait(void) {
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
}

static int ata_poll(int check_error) {
    uint8_t status;
    do {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    } while (status & ATA_SR_BSY);

    if (check_error && (status & ATA_SR_ERR)) {
        return 0;
    }

    return (status & ATA_SR_DRQ) != 0;
}

static int ata_wait_not_busy(int check_error) {
    uint8_t status;
    do {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    } while (status & ATA_SR_BSY);

    if (check_error && (status & ATA_SR_ERR)) {
        return 0;
    }

    return 1;
}

static void ata_select_drive(uint8_t drive, uint32_t lba) {
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL,
         0xE0 | ((drive & 0x1) << 4) | ((lba >> 24) & 0x0F));
    ata_io_wait();
}

int ata_identify(uint8_t drive, ata_device_info_t *info) {
    uint16_t identify_data[256];

    if (info) {
        info->present = 0;
        info->drive = drive;
        info->sector_count = 0;
        info->model[0] = '\0';
    }

    ata_select_drive(drive, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) == 0) {
        return 0;
    }

    if (!ata_poll(1)) {
        return 0;
    }

    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }

    if (info) {
        info->present = 1;
        info->drive = drive;
        info->sector_count =
            ((uint32_t)identify_data[61] << 16) | identify_data[60];

        for (int i = 0; i < 20; i++) {
            info->model[i * 2] = (char)(identify_data[27 + i] >> 8);
            info->model[(i * 2) + 1] = (char)(identify_data[27 + i] & 0xFF);
        }
        info->model[40] = '\0';
        for (int i = 39; i >= 0 && info->model[i] == ' '; i--) {
            info->model[i] = '\0';
        }
    }

    return 1;
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, void *buffer) {
    uint16_t *target = (uint16_t *)buffer;

    for (uint8_t sector = 0; sector < sector_count; sector++) {
        uint32_t current_lba = lba + sector;
        ata_select_drive(drive, current_lba);
        outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA0, current_lba & 0xFF);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (current_lba >> 8) & 0xFF);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (current_lba >> 16) & 0xFF);
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

        if (!ata_poll(1)) {
            return 0;
        }

        for (int i = 0; i < 256; i++) {
            target[(sector * 256) + i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        }
        ata_io_wait();
    }

    return 1;
}

int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, const void *buffer) {
    const uint16_t *source = (const uint16_t *)buffer;

    for (uint8_t sector = 0; sector < sector_count; sector++) {
        uint32_t current_lba = lba + sector;
        ata_select_drive(drive, current_lba);
        outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA0, current_lba & 0xFF);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (current_lba >> 8) & 0xFF);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (current_lba >> 16) & 0xFF);
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

        if (!ata_poll(1)) {
            return 0;
        }

        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, source[(sector * 256) + i]);
        }

        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        if (!ata_wait_not_busy(1)) {
            return 0;
        }
        ata_io_wait();
    }

    return 1;
}
