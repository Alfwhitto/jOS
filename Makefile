CC      = gcc
LD      = ld
NASM    = nasm

CFLAGS  = -m32 -mno-mmx -mno-sse -mno-sse2 -ffreestanding -fno-pic -fno-pie -nostdlib -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c
LDFLAGS = -m elf_i386 -T src/linker.ld

SRC_DIR   = src
BUILD_DIR = build

OBJ = $(BUILD_DIR)/kernel_entry.o \
      $(BUILD_DIR)/ata.o          \
      $(BUILD_DIR)/debug.o        \
      $(BUILD_DIR)/elf.o          \
      $(BUILD_DIR)/fat.o          \
      $(BUILD_DIR)/kernel.o       \
      $(BUILD_DIR)/gdt.o          \
      $(BUILD_DIR)/idt.o          \
      $(BUILD_DIR)/kheap.o        \
      $(BUILD_DIR)/monitor.o      \
      $(BUILD_DIR)/net.o          \
      $(BUILD_DIR)/pci.o          \
      $(BUILD_DIR)/paging.o       \
      $(BUILD_DIR)/keyboard.o     \
      $(BUILD_DIR)/rtl8139.o      \
      $(BUILD_DIR)/timer.o        \
      $(BUILD_DIR)/serial.o       \
      $(BUILD_DIR)/pmm.o          \
      $(BUILD_DIR)/embedded_test_program_blob.o

QEMU    ?= qemu-system-i386
QEMU_IMG ?= qemu-img
ROOTFS  ?= rootfs
ROOTFS_IMG ?= rootfs.img
ROOTFS_VMDK ?= rootfs.vmdk
OS_IMG ?= os.img
OS_VMDK ?= os.vmdk
OS_DISK_SIZE ?= 64M

all: $(BUILD_DIR) $(OS_VMDK) $(ROOTFS_VMDK)

$(ROOTFS_IMG):
	python3 tools/build_rootfs.py $(ROOTFS) $(ROOTFS_IMG)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/boot.bin: $(SRC_DIR)/boot.asm $(BUILD_DIR)/kernel_sectors.inc
	$(NASM) $(SRC_DIR)/boot.asm -f bin -o $(BUILD_DIR)/boot.bin

$(BUILD_DIR)/vmware_boot.bin: $(SRC_DIR)/boot.asm $(BUILD_DIR)/kernel_sectors.inc
	$(NASM) -D KERNEL_PRELOADED=1 $(SRC_DIR)/boot.asm -f bin -o $(BUILD_DIR)/vmware_boot.bin

$(BUILD_DIR)/stage2_sectors.inc: $(BUILD_DIR)/vmware_boot.bin
	printf 'STAGE2_SECTORS equ %s\n' "$$((($$(wc -c < $(BUILD_DIR)/vmware_boot.bin) + 511) / 512))" > $(BUILD_DIR)/stage2_sectors.inc

$(BUILD_DIR)/mbr.bin: $(SRC_DIR)/mbr.asm $(BUILD_DIR)/kernel_sectors.inc $(BUILD_DIR)/stage2_sectors.inc
	$(NASM) $(SRC_DIR)/mbr.asm -f bin -o $(BUILD_DIR)/mbr.bin

$(BUILD_DIR)/kernel.bin: $(OBJ)
	$(LD) $(LDFLAGS) -o $(BUILD_DIR)/kernel.bin $(OBJ)

$(BUILD_DIR)/kernel_sectors.inc: $(BUILD_DIR)/kernel.bin
	printf 'KERNEL_SECTORS equ %s\n' "$$((($$(wc -c < $(BUILD_DIR)/kernel.bin) + 511) / 512))" > $(BUILD_DIR)/kernel_sectors.inc

$(BUILD_DIR)/os_image.bin: $(BUILD_DIR)/boot.bin $(BUILD_DIR)/kernel.bin
	cat $(BUILD_DIR)/boot.bin $(BUILD_DIR)/kernel.bin > $(BUILD_DIR)/os_image.bin
	truncate -s 66048 $(BUILD_DIR)/os_image.bin

$(OS_IMG): $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/vmware_boot.bin $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/stage2_sectors.inc
	rm -f $(OS_IMG)
	truncate -s $(OS_DISK_SIZE) $(OS_IMG)
	dd if=$(BUILD_DIR)/mbr.bin of=$(OS_IMG) conv=notrunc
	dd if=$(BUILD_DIR)/vmware_boot.bin of=$(OS_IMG) bs=512 seek=1 conv=notrunc
	STAGE2_SECTORS=$$((($$(wc -c < $(BUILD_DIR)/vmware_boot.bin) + 511) / 512)); \
	dd if=$(BUILD_DIR)/kernel.bin of=$(OS_IMG) bs=512 seek=$$((1 + $$STAGE2_SECTORS)) conv=notrunc

$(OS_VMDK): $(OS_IMG)
	$(QEMU_IMG) convert -f raw -O vmdk -o subformat=monolithicSparse $(OS_IMG) $(OS_VMDK)

$(ROOTFS_VMDK): $(ROOTFS_IMG)
	$(QEMU_IMG) convert -f raw -O vmdk -o subformat=monolithicSparse $(ROOTFS_IMG) $(ROOTFS_VMDK)

$(BUILD_DIR)/kernel_entry.o: $(SRC_DIR)/kernel_entry.asm
	$(NASM) $(SRC_DIR)/kernel_entry.asm -f elf32 -o $(BUILD_DIR)/kernel_entry.o

$(BUILD_DIR)/kernel.o: $(SRC_DIR)/kernel.c
	$(CC) $(CFLAGS) $(SRC_DIR)/kernel.c -o $(BUILD_DIR)/kernel.o

$(BUILD_DIR)/ata.o: $(SRC_DIR)/ata.c
	$(CC) $(CFLAGS) $(SRC_DIR)/ata.c -o $(BUILD_DIR)/ata.o

$(BUILD_DIR)/debug.o: $(SRC_DIR)/debug.c
	$(CC) $(CFLAGS) $(SRC_DIR)/debug.c -o $(BUILD_DIR)/debug.o

$(BUILD_DIR)/elf.o: $(SRC_DIR)/elf.c
	$(CC) $(CFLAGS) $(SRC_DIR)/elf.c -o $(BUILD_DIR)/elf.o

$(BUILD_DIR)/fat.o: $(SRC_DIR)/fat.c
	$(CC) $(CFLAGS) $(SRC_DIR)/fat.c -o $(BUILD_DIR)/fat.o

$(BUILD_DIR)/gdt.o: $(SRC_DIR)/gdt.c
	$(CC) $(CFLAGS) $(SRC_DIR)/gdt.c -o $(BUILD_DIR)/gdt.o

$(BUILD_DIR)/idt.o: $(SRC_DIR)/idt.c
	$(CC) $(CFLAGS) $(SRC_DIR)/idt.c -o $(BUILD_DIR)/idt.o

$(BUILD_DIR)/kheap.o: $(SRC_DIR)/kheap.c
	$(CC) $(CFLAGS) $(SRC_DIR)/kheap.c -o $(BUILD_DIR)/kheap.o

$(BUILD_DIR)/monitor.o: $(SRC_DIR)/monitor.c
	$(CC) $(CFLAGS) $(SRC_DIR)/monitor.c -o $(BUILD_DIR)/monitor.o

$(BUILD_DIR)/net.o: $(SRC_DIR)/net.c
	$(CC) $(CFLAGS) $(SRC_DIR)/net.c -o $(BUILD_DIR)/net.o

$(BUILD_DIR)/pci.o: $(SRC_DIR)/pci.c
	$(CC) $(CFLAGS) $(SRC_DIR)/pci.c -o $(BUILD_DIR)/pci.o

$(BUILD_DIR)/paging.o: $(SRC_DIR)/paging.c
	$(CC) $(CFLAGS) $(SRC_DIR)/paging.c -o $(BUILD_DIR)/paging.o

$(BUILD_DIR)/keyboard.o: $(SRC_DIR)/keyboard.c
	$(CC) $(CFLAGS) $(SRC_DIR)/keyboard.c -o $(BUILD_DIR)/keyboard.o

$(BUILD_DIR)/rtl8139.o: $(SRC_DIR)/rtl8139.c
	$(CC) $(CFLAGS) $(SRC_DIR)/rtl8139.c -o $(BUILD_DIR)/rtl8139.o

$(BUILD_DIR)/timer.o: $(SRC_DIR)/timer.c
	$(CC) $(CFLAGS) $(SRC_DIR)/timer.c -o $(BUILD_DIR)/timer.o

$(BUILD_DIR)/serial.o: $(SRC_DIR)/serial.c
	$(CC) $(CFLAGS) $(SRC_DIR)/serial.c -o $(BUILD_DIR)/serial.o

$(BUILD_DIR)/pmm.o: $(SRC_DIR)/pmm.c
	$(CC) $(CFLAGS) $(SRC_DIR)/pmm.c -o $(BUILD_DIR)/pmm.o

$(BUILD_DIR)/embedded_test_program.o: $(SRC_DIR)/embedded_test_program.c
	$(CC) $(CFLAGS) $(SRC_DIR)/embedded_test_program.c -o $(BUILD_DIR)/embedded_test_program.o

$(BUILD_DIR)/embedded_test_program.elf: $(BUILD_DIR)/embedded_test_program.o
	$(LD) -m elf_i386 -T $(SRC_DIR)/embedded_test_program.ld -o $(BUILD_DIR)/embedded_test_program.elf $(BUILD_DIR)/embedded_test_program.o

$(BUILD_DIR)/embedded_test_program_blob.o: $(BUILD_DIR)/embedded_test_program.elf
	objcopy -I binary -O elf32-i386 -B i386 $(BUILD_DIR)/embedded_test_program.elf $(BUILD_DIR)/embedded_test_program_blob.o

refresh-rootfs:
	rm -f $(ROOTFS_IMG)
	$(MAKE) $(ROOTFS_IMG)

run: $(BUILD_DIR) $(BUILD_DIR)/os_image.bin $(ROOTFS_IMG)
	$(QEMU) -vga std -drive format=raw,file=$(BUILD_DIR)/os_image.bin,if=ide,index=0 -drive format=raw,file=$(ROOTFS_IMG),if=ide,index=1 -netdev user,id=net0 -device rtl8139,netdev=net0 -display gtk

clean:
	rm -rf $(BUILD_DIR) $(OS_IMG) $(OS_VMDK) $(ROOTFS_VMDK)
