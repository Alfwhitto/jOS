# jOS

jOS is a 32-bit hobby operating system built from scratch for x86 PCs. It includes a custom bootloader, monolithic kernel, basic drivers, a shell, and experimental networking support.

---

## Features

- Custom BIOS bootloader (INT 13h LBA loading)
- 32-bit protected mode kernel
- GDT and IDT setup
- Interrupt handling (ISRs and IRQs)
- Physical memory manager (PMM)
- Paging system
- Kernel heap allocator
- ATA disk driver
- FAT filesystem support for rootfs
- Keyboard driver
- VGA text output
- Serial logging (COM1)
- Basic shell with commands
- ICMP ping support (IPv4 and IPv6 via RTL8139 NIC)
- Embedded ELF program loader

---

## Architecture

- Target: i686 (32-bit x86)
- Boot: BIOS (no UEFI support)
- Disk format: raw image split into bootloader + kernel
- Root filesystem: FAT on secondary ATA disk

---

## Build

```bash
make clean
make
make run
```

## Dependencies
For Debian Distributions (Debian, Ubuntu, Mint):
```bash
sudo apt update
sudo apt install -y \
build-essential \
nasm \
make \
python3 \
qemu-system-x86 \
qemu-utils \
binutils \
gcc \
gdb \
hexdump
```

For Arch Based Distributions:
```bash
sudo pacman -Syu
sudo pacman -S \
base-devel \
nasm \
make \
python \
qemu-system-x86 \
qemu-img \
binutils \
gcc \
gdb
```

For Alpine Distributions:
```bash
sudo apk update
sudo apk add \
build-base \
nasm \
make \
python3 \
qemu-system-i386 \
qemu-img \
binutils \
gcc \
gdb
```
**NOTE: Alpine uses musl libc. This means that some debugging tools may behave differently, and a cross-compiler (i686-elf-gcc) is recommended for serious kernel work!**

Please enjoy this project, and leave feedback if you can.

Copyright © 2026 Alfwhitto

All rights reserved.
