#include "fat.h"
#include "ata.h"
#include "debug.h"
#include "kheap.h"

#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_LFN       0x0F

typedef struct fat_bpb_common {
    uint8_t jump[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} __attribute__((packed)) fat_bpb_common_t;

typedef struct fat_bpb32 {
    fat_bpb_common_t common;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed)) fat_bpb32_t;

typedef struct fat_dir_entry {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_t;

typedef struct fat_fs {
    uint8_t mounted;
    uint8_t drive;
    uint8_t fat_type;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint32_t fat_size;
    uint32_t total_sectors;
    uint32_t root_dir_sectors;
    uint32_t first_data_sector;
    uint32_t first_fat_sector;
    uint32_t root_dir_sector;
    uint32_t root_cluster;
    uint32_t cluster_count;
} fat_fs_t;

typedef struct find_ctx {
    uint8_t target[11];
    fat_dir_entry_t entry;
    uint32_t lba;
    uint32_t index_in_sector;
    int found;
    int want_free;
} find_ctx_t;

typedef struct list_ctx {
    fat_file_info_t *entries;
    uint32_t max_entries;
    uint32_t count;
} list_ctx_t;

static fat_fs_t fat_fs;

static uint32_t min_u32(uint32_t a, uint32_t b) { return (a < b) ? a : b; }
static uint32_t cluster_to_sector(uint32_t cluster) {
    return fat_fs.first_data_sector + ((cluster - 2) * fat_fs.sectors_per_cluster);
}

static void dir_entry_to_name(const fat_dir_entry_t *entry, char *name_out) {
    int out = 0;
    for (int i = 0; i < 8 && entry->name[i] != ' '; i++) {
        name_out[out++] = (char)entry->name[i];
    }
    if (entry->name[8] != ' ') {
        name_out[out++] = '.';
        for (int i = 8; i < 11 && entry->name[i] != ' '; i++) {
            name_out[out++] = (char)entry->name[i];
        }
    }
    name_out[out] = '\0';
}

static int fat_format_name_83(const char *input, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) {
        out[i] = ' ';
    }

    int part = 0;
    int index = 0;
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        if (c == '.') {
            part = 1;
            index = 0;
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }
        if (part == 0) {
            if (index >= 8) return 0;
            out[index++] = (uint8_t)c;
        } else {
            if (index >= 3) return 0;
            out[8 + index++] = (uint8_t)c;
        }
    }
    return 1;
}

static uint32_t fat_get_total_sectors(const fat_bpb_common_t *bpb) {
    return bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
}

static uint32_t fat_get_fat_size(const fat_bpb_common_t *bpb) {
    return bpb->fat_size_16 ? bpb->fat_size_16 : ((const fat_bpb32_t *)bpb)->fat_size_32;
}

static int read_sector(uint32_t lba, uint8_t *buffer) {
    return ata_read_sectors(fat_fs.drive, lba, 1, buffer);
}

static int write_sector(uint32_t lba, const uint8_t *buffer) {
    return ata_write_sectors(fat_fs.drive, lba, 1, buffer);
}

static uint32_t fat_get_next_cluster(uint32_t cluster) {
    uint8_t sector[512];
    uint32_t fat_offset = (fat_fs.fat_type == 16) ? cluster * 2 : cluster * 4;
    uint32_t fat_sector = fat_fs.first_fat_sector + (fat_offset / fat_fs.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fat_fs.bytes_per_sector;

    if (!read_sector(fat_sector, sector)) return 0;
    if (fat_fs.fat_type == 16) return *(uint16_t *)(sector + entry_offset);
    return (*(uint32_t *)(sector + entry_offset)) & 0x0FFFFFFF;
}

static int fat_set_cluster_value(uint32_t cluster, uint32_t value) {
    uint8_t sector[512];
    uint32_t fat_offset = (fat_fs.fat_type == 16) ? cluster * 2 : cluster * 4;
    uint32_t sector_index = fat_offset / fat_fs.bytes_per_sector;
    uint32_t entry_offset = fat_offset % fat_fs.bytes_per_sector;

    for (uint8_t copy = 0; copy < fat_fs.fat_count; copy++) {
        uint32_t lba = fat_fs.first_fat_sector + (copy * fat_fs.fat_size) + sector_index;
        if (!read_sector(lba, sector)) {
            kdebug_write("    [fat] FAT update read failed at LBA ");
            kdebug_write_dec(lba);
            kdebug_put('\n');
            return 0;
        }
        if (fat_fs.fat_type == 16) {
            *(uint16_t *)(sector + entry_offset) = (uint16_t)value;
        } else {
            uint32_t current = *(uint32_t *)(sector + entry_offset);
            current &= 0xF0000000;
            current |= (value & 0x0FFFFFFF);
            *(uint32_t *)(sector + entry_offset) = current;
        }
        if (!write_sector(lba, sector)) {
            kdebug_write("    [fat] FAT update write failed at LBA ");
            kdebug_write_dec(lba);
            kdebug_put('\n');
            return 0;
        }
    }
    return 1;
}

static int fat_is_end_of_chain(uint32_t cluster) {
    return fat_fs.fat_type == 16 ? (cluster >= 0xFFF8) : (cluster >= 0x0FFFFFF8);
}

static uint32_t fat_allocate_cluster(void) {
    for (uint32_t cluster = 2; cluster < fat_fs.cluster_count + 2; cluster++) {
        if (fat_get_next_cluster(cluster) == 0) {
            if (fat_set_cluster_value(cluster, fat_fs.fat_type == 16 ? 0xFFFF : 0x0FFFFFFF)) {
                return cluster;
            }
            return 0;
        }
    }
    return 0;
}

static void fat_free_chain(uint32_t start_cluster) {
    uint32_t current = start_cluster;
    while (current >= 2 && !fat_is_end_of_chain(current)) {
        uint32_t next = fat_get_next_cluster(current);
        fat_set_cluster_value(current, 0);
        current = next;
    }
    if (current >= 2) fat_set_cluster_value(current, 0);
}

static int fat_find_callback(fat_dir_entry_t *entry, uint32_t lba, uint32_t index, void *ctx_ptr) {
    find_ctx_t *ctx = (find_ctx_t *)ctx_ptr;
    if (ctx->want_free) {
        if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
            ctx->entry = *entry;
            ctx->lba = lba;
            ctx->index_in_sector = index;
            ctx->found = 1;
            return 0;
        }
        return 1;
    }

    if (entry->name[0] == 0x00) return 0;
    if (entry->name[0] == 0xE5 || entry->attr == FAT_ATTR_LFN) return 1;

    int match = 1;
    for (int i = 0; i < 11; i++) {
        if (entry->name[i] != ctx->target[i]) {
            match = 0;
            break;
        }
    }
    if (match) {
        ctx->entry = *entry;
        ctx->lba = lba;
        ctx->index_in_sector = index;
        ctx->found = 1;
        return 0;
    }
    return 1;
}

static int fat_list_callback(fat_dir_entry_t *entry, uint32_t lba, uint32_t index, void *ctx_ptr) {
    (void)lba;
    (void)index;
    list_ctx_t *list = (list_ctx_t *)ctx_ptr;

    if (entry->name[0] == 0x00) {
        return 0;
    }

    if (entry->name[0] == 0xE5 || entry->attr == FAT_ATTR_LFN) {
        return 1;
    }

    if (list->count < list->max_entries) {
        dir_entry_to_name(entry, list->entries[list->count].name);
        list->entries[list->count].size = entry->file_size;
        list->entries[list->count].is_directory = (entry->attr & FAT_ATTR_DIRECTORY) != 0;
    }
    list->count++;
    return 1;
}

static int fat_iterate_root(int (*callback)(fat_dir_entry_t *, uint32_t, uint32_t, void *), void *ctx) {
    uint8_t sector[512];
    if (fat_fs.fat_type == 16) {
        for (uint32_t sector_offset = 0; sector_offset < fat_fs.root_dir_sectors; sector_offset++) {
            uint32_t lba = fat_fs.root_dir_sector + sector_offset;
            if (!read_sector(lba, sector)) return 0;
            fat_dir_entry_t *entries = (fat_dir_entry_t *)sector;
            for (uint32_t i = 0; i < fat_fs.bytes_per_sector / sizeof(fat_dir_entry_t); i++) {
                if (!callback(&entries[i], lba, i, ctx)) return 1;
            }
        }
        return 1;
    }

    uint32_t cluster = fat_fs.root_cluster;
    while (cluster >= 2) {
        uint32_t first_sector = cluster_to_sector(cluster);
        for (uint32_t sector_offset = 0; sector_offset < fat_fs.sectors_per_cluster; sector_offset++) {
            uint32_t lba = first_sector + sector_offset;
            if (!read_sector(lba, sector)) return 0;
            fat_dir_entry_t *entries = (fat_dir_entry_t *)sector;
            for (uint32_t i = 0; i < fat_fs.bytes_per_sector / sizeof(fat_dir_entry_t); i++) {
                if (!callback(&entries[i], lba, i, ctx)) return 1;
            }
        }
        uint32_t next = fat_get_next_cluster(cluster);
        if (fat_is_end_of_chain(next) || next == 0) break;
        cluster = next;
    }
    return 1;
}

static int fat_find_entry(const char *name, find_ctx_t *ctx) {
    if (!fat_format_name_83(name, ctx->target)) return 0;
    ctx->found = 0;
    ctx->want_free = 0;
    return fat_iterate_root(fat_find_callback, ctx) && ctx->found;
}

static int fat_find_free_entry(find_ctx_t *ctx) {
    ctx->found = 0;
    ctx->want_free = 1;
    return fat_iterate_root(fat_find_callback, ctx) && ctx->found;
}

static int fat_write_dir_entry(uint32_t lba, uint32_t index, const fat_dir_entry_t *entry) {
    uint8_t sector[512];
    if (!read_sector(lba, sector)) return 0;
    ((fat_dir_entry_t *)sector)[index] = *entry;
    return write_sector(lba, sector);
}

int fat_mount(uint8_t drive) {
    uint8_t sector[512];
    uint32_t start_lba = 0;
    fat_bpb_common_t *bpb;

    fat_fs.mounted = 0;
    fat_fs.drive = drive;
    if (!ata_read_sectors(drive, 0, 1, sector)) return 0;

    bpb = (fat_bpb_common_t *)sector;
    if (!((sector[0] == 0xEB || sector[0] == 0xE9) && bpb->bytes_per_sector != 0)) {
        uint32_t partition_lba = *(uint32_t *)(sector + 454);
        if (partition_lba == 0) return 0;
        start_lba = partition_lba;
        if (!ata_read_sectors(drive, start_lba, 1, sector)) return 0;
        bpb = (fat_bpb_common_t *)sector;
    }

    fat_fs.bytes_per_sector = bpb->bytes_per_sector;
    fat_fs.sectors_per_cluster = bpb->sectors_per_cluster;
    fat_fs.reserved_sector_count = bpb->reserved_sector_count;
    fat_fs.fat_count = bpb->fat_count;
    fat_fs.fat_size = fat_get_fat_size(bpb);
    fat_fs.total_sectors = fat_get_total_sectors(bpb);
    fat_fs.root_dir_sectors =
        ((bpb->root_entry_count * 32) + (fat_fs.bytes_per_sector - 1)) / fat_fs.bytes_per_sector;
    fat_fs.first_fat_sector = start_lba + fat_fs.reserved_sector_count;
    fat_fs.first_data_sector =
        start_lba + fat_fs.reserved_sector_count + (fat_fs.fat_count * fat_fs.fat_size) +
        fat_fs.root_dir_sectors;

    uint32_t data_sectors = fat_fs.total_sectors -
        (fat_fs.reserved_sector_count + (fat_fs.fat_count * fat_fs.fat_size) + fat_fs.root_dir_sectors);
    fat_fs.cluster_count = data_sectors / fat_fs.sectors_per_cluster;

    if (fat_fs.cluster_count < 65525) {
        fat_fs.fat_type = 16;
        fat_fs.root_dir_sector =
            start_lba + fat_fs.reserved_sector_count + (fat_fs.fat_count * fat_fs.fat_size);
        fat_fs.root_cluster = 0;
    } else {
        fat_fs.fat_type = 32;
        fat_fs.root_cluster = ((fat_bpb32_t *)sector)->root_cluster;
        fat_fs.root_dir_sector = cluster_to_sector(fat_fs.root_cluster);
    }

    fat_fs.mounted = 1;
    kdebug_write("    [fat] FAT type: ");
    kdebug_write_dec(fat_fs.fat_type);
    kdebug_put('\n');
    kdebug_write("    [fat] Cluster count: ");
    kdebug_write_dec(fat_fs.cluster_count);
    kdebug_put('\n');
    return 1;
}

int fat_is_mounted(void) { return fat_fs.mounted; }

int fat_list_root(fat_file_info_t *entries, uint32_t max_entries, uint32_t *entry_count) {
    if (!fat_fs.mounted) return 0;

    list_ctx_t ctx = { entries, max_entries, 0 };

    if (!fat_iterate_root(fat_list_callback, &ctx)) return 0;
    if (entry_count) *entry_count = ctx.count;
    return 1;
}

int fat_read_file(const char *name, uint8_t **buffer, uint32_t *size_out) {
    if (!fat_fs.mounted) return 0;

    find_ctx_t ctx;
    if (!fat_find_entry(name, &ctx)) return 0;

    uint32_t first_cluster = ((uint32_t)ctx.entry.first_cluster_hi << 16) | ctx.entry.first_cluster_lo;
    uint32_t file_size = ctx.entry.file_size;
    uint8_t *data = (uint8_t *)kmalloc(file_size + 1);
    uint8_t *sector_buffer = (uint8_t *)kmalloc(fat_fs.bytes_per_sector);
    if (!data || !sector_buffer) {
        if (data) kfree(data);
        if (sector_buffer) kfree(sector_buffer);
        return 0;
    }

    uint32_t bytes_read = 0;
    uint32_t cluster = first_cluster;
    while (cluster >= 2 && !fat_is_end_of_chain(cluster) && bytes_read < file_size) {
        uint32_t first_sector = cluster_to_sector(cluster);
        for (uint32_t sector = 0; sector < fat_fs.sectors_per_cluster && bytes_read < file_size; sector++) {
            if (!read_sector(first_sector + sector, sector_buffer)) {
                kfree(sector_buffer); kfree(data); return 0;
            }
            uint32_t copy_size = min_u32(fat_fs.bytes_per_sector, file_size - bytes_read);
            for (uint32_t i = 0; i < copy_size; i++) data[bytes_read + i] = sector_buffer[i];
            bytes_read += copy_size;
        }
        uint32_t next = fat_get_next_cluster(cluster);
        if (fat_is_end_of_chain(next)) break;
        cluster = next;
    }

    data[file_size] = '\0';
    kfree(sector_buffer);
    *buffer = data;
    if (size_out) *size_out = file_size;
    return 1;
}

int fat_write_file(const char *name, const uint8_t *data, uint32_t size) {
    if (!fat_fs.mounted) return 0;

    find_ctx_t existing;
    int has_existing = fat_find_entry(name, &existing);
    fat_dir_entry_t entry;
    if (has_existing) {
        uint32_t first_cluster = ((uint32_t)existing.entry.first_cluster_hi << 16) | existing.entry.first_cluster_lo;
        if (first_cluster >= 2) fat_free_chain(first_cluster);
        entry = existing.entry;
    } else {
        find_ctx_t free_entry;
        if (!fat_find_free_entry(&free_entry)) {
            kdebug_write_line("    [fat] write failed: no free root directory entry.");
            return 0;
        }
        for (int i = 0; i < (int)sizeof(entry); i++) ((uint8_t *)&entry)[i] = 0;
        if (!fat_format_name_83(name, entry.name)) {
            kdebug_write_line("    [fat] write failed: filename not representable as 8.3.");
            return 0;
        }
        existing.lba = free_entry.lba;
        existing.index_in_sector = free_entry.index_in_sector;
    }

    uint32_t cluster_size = fat_fs.bytes_per_sector * fat_fs.sectors_per_cluster;
    uint32_t clusters_needed = (size + cluster_size - 1) / cluster_size;
    uint32_t first_cluster = 0;
    uint32_t previous_cluster = 0;

    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint32_t cluster = fat_allocate_cluster();
        if (cluster == 0) {
            if (first_cluster) fat_free_chain(first_cluster);
            kdebug_write_line("    [fat] write failed: unable to allocate cluster.");
            return 0;
        }
        if (first_cluster == 0) first_cluster = cluster;
        else if (!fat_set_cluster_value(previous_cluster, cluster)) {
            kdebug_write_line("    [fat] write failed: unable to link cluster chain.");
            return 0;
        }
        previous_cluster = cluster;
    }
    if (previous_cluster) {
        if (!fat_set_cluster_value(previous_cluster, fat_fs.fat_type == 16 ? 0xFFFF : 0x0FFFFFFF)) {
            kdebug_write_line("    [fat] write failed: unable to terminate cluster chain.");
            return 0;
        }
    }

    uint8_t *sector_buffer = (uint8_t *)kmalloc(fat_fs.bytes_per_sector);
    if (!sector_buffer) {
        if (first_cluster) fat_free_chain(first_cluster);
        kdebug_write_line("    [fat] write failed: unable to allocate sector buffer.");
        return 0;
    }

    uint32_t bytes_written = 0;
    uint32_t cluster = first_cluster;
    while (cluster >= 2 && bytes_written < size) {
        uint32_t first_sector = cluster_to_sector(cluster);
        for (uint32_t sector = 0; sector < fat_fs.sectors_per_cluster; sector++) {
            for (uint32_t i = 0; i < fat_fs.bytes_per_sector; i++) sector_buffer[i] = 0;
            uint32_t chunk = min_u32(fat_fs.bytes_per_sector, size - bytes_written);
            for (uint32_t i = 0; i < chunk; i++) sector_buffer[i] = data[bytes_written + i];
            if (!write_sector(first_sector + sector, sector_buffer)) {
                kdebug_write("    [fat] write failed: sector write error at LBA ");
                kdebug_write_dec(first_sector + sector);
                kdebug_put('\n');
                kfree(sector_buffer); return 0;
            }
            bytes_written += chunk;
            if (bytes_written >= size) break;
        }
        if (bytes_written >= size) break;
        cluster = fat_get_next_cluster(cluster);
    }
    kfree(sector_buffer);

    entry.attr = 0;
    entry.first_cluster_lo = (uint16_t)(first_cluster & 0xFFFF);
    entry.first_cluster_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
    entry.file_size = size;
    if (!fat_write_dir_entry(existing.lba, existing.index_in_sector, &entry)) {
        kdebug_write_line("    [fat] write failed: unable to commit directory entry.");
        return 0;
    }

    return 1;
}

int fat_delete_file(const char *name) {
    if (!fat_fs.mounted) return 0;

    find_ctx_t ctx;
    if (!fat_find_entry(name, &ctx)) return 0;
    uint32_t first_cluster = ((uint32_t)ctx.entry.first_cluster_hi << 16) | ctx.entry.first_cluster_lo;
    if (first_cluster >= 2) fat_free_chain(first_cluster);

    uint8_t sector[512];
    if (!read_sector(ctx.lba, sector)) return 0;
    ((fat_dir_entry_t *)sector)[ctx.index_in_sector].name[0] = 0xE5;
    return write_sector(ctx.lba, sector);
}
