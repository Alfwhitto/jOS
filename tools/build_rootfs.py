#!/usr/bin/env python3
import math
import struct
import sys
from pathlib import Path

BYTES_PER_SECTOR = 512
SECTORS_PER_CLUSTER = 4
RESERVED_SECTORS = 1
FAT_COUNT = 2
ROOT_ENTRY_COUNT = 512
MEDIA_DESCRIPTOR = 0xF8
SECTORS_PER_TRACK = 32
HEADS = 64
TOTAL_SECTORS = 32768
FAT_SIZE_SECTORS = 32
VOLUME_LABEL = b"ROOTFS     "
FS_TYPE = b"FAT16   "


def to_8dot3(name: str) -> bytes:
    upper = name.upper()
    if "." in upper:
        stem, ext = upper.split(".", 1)
    else:
        stem, ext = upper, ""
    if not stem or len(stem) > 8 or len(ext) > 3:
        raise ValueError(f"unsupported filename for 8.3 image: {name}")
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&"
    if any(ch not in allowed for ch in stem + ext):
        raise ValueError(f"unsupported character in filename: {name}")
    return stem.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def build_boot_sector(total_sectors: int) -> bytes:
    boot = bytearray(BYTES_PER_SECTOR)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"MKROOTFS"
    struct.pack_into("<H", boot, 11, BYTES_PER_SECTOR)
    struct.pack_into("<B", boot, 13, SECTORS_PER_CLUSTER)
    struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
    struct.pack_into("<B", boot, 16, FAT_COUNT)
    struct.pack_into("<H", boot, 17, ROOT_ENTRY_COUNT)
    struct.pack_into("<H", boot, 19, total_sectors if total_sectors < 0x10000 else 0)
    struct.pack_into("<B", boot, 21, MEDIA_DESCRIPTOR)
    struct.pack_into("<H", boot, 22, FAT_SIZE_SECTORS)
    struct.pack_into("<H", boot, 24, SECTORS_PER_TRACK)
    struct.pack_into("<H", boot, 26, HEADS)
    struct.pack_into("<I", boot, 28, 0)
    struct.pack_into("<I", boot, 32, total_sectors if total_sectors >= 0x10000 else 0)
    boot[36] = 0x80
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x1234ABCD)
    boot[43:54] = VOLUME_LABEL
    boot[54:62] = FS_TYPE
    boot[510:512] = b"\x55\xAA"
    return bytes(boot)


def make_dir_entry(raw_name: bytes, first_cluster: int, size: int) -> bytes:
    entry = bytearray(32)
    entry[0:11] = raw_name
    entry[11] = 0x20
    struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: build_rootfs.py <source_dir> <output_img>", file=sys.stderr)
        return 1

    source_dir = Path(sys.argv[1])
    output_img = Path(sys.argv[2])
    if not source_dir.is_dir():
        print(f"source directory not found: {source_dir}", file=sys.stderr)
        return 1

    root_dir_sectors = (ROOT_ENTRY_COUNT * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    first_data_sector = RESERVED_SECTORS + FAT_COUNT * FAT_SIZE_SECTORS + root_dir_sectors
    cluster_size = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
    image_size = TOTAL_SECTORS * BYTES_PER_SECTOR

    files = [path for path in sorted(source_dir.iterdir()) if path.is_file()]

    image = bytearray(image_size)
    image[0:BYTES_PER_SECTOR] = build_boot_sector(TOTAL_SECTORS)

    fat_entries = [0] * (FAT_SIZE_SECTORS * BYTES_PER_SECTOR // 2)
    fat_entries[0] = 0xFFF8
    fat_entries[1] = 0xFFFF

    root_offset = (RESERVED_SECTORS + FAT_COUNT * FAT_SIZE_SECTORS) * BYTES_PER_SECTOR
    next_cluster = 2
    root_index = 0

    for path in files:
        contents = path.read_bytes()
        raw_name = to_8dot3(path.name)
        clusters_needed = max(1, math.ceil(len(contents) / cluster_size))
        first_cluster = next_cluster

        for i in range(clusters_needed):
            cluster = next_cluster + i
            fat_entries[cluster] = 0xFFFF if i == clusters_needed - 1 else cluster + 1
            chunk = contents[i * cluster_size:(i + 1) * cluster_size]
            sector_index = first_data_sector + (cluster - 2) * SECTORS_PER_CLUSTER
            offset = sector_index * BYTES_PER_SECTOR
            image[offset:offset + len(chunk)] = chunk

        entry = make_dir_entry(raw_name, first_cluster, len(contents))
        entry_offset = root_offset + root_index * 32
        image[entry_offset:entry_offset + 32] = entry
        root_index += 1
        next_cluster += clusters_needed

    for fat_copy in range(FAT_COUNT):
        fat_offset = (RESERVED_SECTORS + fat_copy * FAT_SIZE_SECTORS) * BYTES_PER_SECTOR
        for i, value in enumerate(fat_entries):
            struct.pack_into("<H", image, fat_offset + i * 2, value & 0xFFFF)

    output_img.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
