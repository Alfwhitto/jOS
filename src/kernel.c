#include "monitor.h"
#include "ata.h"
#include "elf.h"
#include "fat.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "net.h"
#include "timer.h"
#include "serial.h"
#include "debug.h"
#include "kheap.h"
#include "memory_map.h"
#include "pmm.h"
#include "paging.h"

// Declare the external linker symbol tracking the absolute end boundary of the kernel image.
extern char end[];

// Make the tracking variable global so functions like execute_command can parse it.
uint32_t kernel_end = 0;

extern uint8_t _binary_build_embedded_test_program_elf_start[];
extern uint8_t _binary_build_embedded_test_program_elf_end[];
extern uint8_t _binary_build_embedded_test_program_elf_size[];

static void exec_api_write_line(const char *message) {
    kdebug_write_line(message);
}

static void exec_api_write(const char *message) {
    kdebug_write(message);
}

static void exec_api_write_hex(uint32_t value) {
    kdebug_write_hex(value);
}

static void exec_api_write_dec(uint32_t value) {
    kdebug_write_dec(value);
}

int kstrcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int kstrncmp(const char *s1, const char *s2, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];
        if (c1 != c2) {
            return c1 - c2;
        }
        if (c1 == '\0') {
            return 0;
        }
    }
    return 0;
}

uint32_t kstrlen(const char *s) {
    uint32_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ') {
        text++;
    }
    return text;
}

static void copy_trimmed_argument(char *destination, uint32_t capacity, const char *source) {
    uint32_t length = 0;
    const char *trimmed = skip_spaces(source);

    while (trimmed[length] != '\0' && length + 1 < capacity) {
        destination[length] = trimmed[length];
        length++;
    }

    while (length > 0 && destination[length - 1] == ' ') {
        length--;
    }

    destination[length] = '\0';
}

void execute_command(const char *input) {
    monitor_write("\n");
    serial_write("Shell Command Executed: ");
    serial_write(input);
    serial_write("\n");

    if (kstrcmp(input, "help") == 0) {
        monitor_write("Available Commands:\n");
        monitor_write("  help   - Show this command reference table\n");
        monitor_write("  clear  - Clear the local VGA screen matrix\n");
        monitor_write("  ping IP - Send an ICMP echo request over rtl8139\n");
        monitor_write("  ping6 IP - Send an ICMPv6 echo request over rtl8139\n");
        monitor_write("  free   - Display active physical RAM memory allocations\n");
        monitor_write("  heap   - Display kernel heap allocator statistics\n");
        monitor_write("  vm     - Display paging translation details for the heap base\n");
        monitor_write("  exec   - Load and run the embedded ELF test program\n");
        monitor_write("  ls     - List files in the mounted rootfs\n");
        monitor_write("  cat F  - Read a file from the mounted rootfs\n");
        monitor_write("  write F TEXT - Create or overwrite a rootfs file\n");
        monitor_write("  rm F   - Delete a rootfs file from the mounted rootfs\n");
    } 
    else if (kstrcmp(input, "clear") == 0) {
        monitor_clear();
    } 
    else if (kstrcmp(input, "ping") == 0) {
        monitor_write("Defaulting to ping 10.0.2.2 through QEMU user networking...\n");
        if (net_ping("10.0.2.2")) {
            monitor_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            monitor_write("Ping reply received from 10.0.2.2.\n");
            monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        } else {
            monitor_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            monitor_write("Ping to 10.0.2.2 failed.\n");
            monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
    }
    else if (kstrncmp(input, "ping ", 5) == 0) {
        char target[32];
        copy_trimmed_argument(target, sizeof(target), input + 5);

        if (kstrcmp(target, "gw") == 0 || kstrcmp(target, "gateway") == 0) {
            target[0] = '1';
            target[1] = '0';
            target[2] = '.';
            target[3] = '0';
            target[4] = '.';
            target[5] = '2';
            target[6] = '.';
            target[7] = '2';
            target[8] = '\0';
        } else if (kstrcmp(target, "self") == 0 || kstrcmp(target, "local") == 0) {
            target[0] = '1';
            target[1] = '0';
            target[2] = '.';
            target[3] = '0';
            target[4] = '.';
            target[5] = '2';
            target[6] = '.';
            target[7] = '1';
            target[8] = '5';
            target[9] = '\0';
        }

        if (target[0] == '\0') {
            monitor_write("Usage: ping 10.0.2.2\n");
            monitor_write("Aliases: ping gateway, ping gw, ping self\n");
            return;
        }

        if (net_ping(target)) {
            monitor_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            monitor_write("Ping reply received.\n");
            monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        } else {
            monitor_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            monitor_write("Ping failed or timed out.\n");
            monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
    } 
    else if (kstrcmp(input, "ping6") == 0) {
        monitor_write("Defaulting to ping6 fec0::2 through QEMU IPv6 user networking...\n");
        if (net_ping6("fec0::2")) {
            monitor_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            monitor_write("ICMPv6 echo reply received from fec0::2.\n");
            monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        } else {
            monitor_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            monitor_write("Ping6 to fec0::2 failed.\n");
            monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
    }
    else if (kstrncmp(input, "ping6 ", 6) == 0) {
        char target[64];
        copy_trimmed_argument(target, sizeof(target), input + 6);

        if (kstrcmp(target, "gw") == 0 || kstrcmp(target, "gateway") == 0) {
            target[0] = 'f';
            target[1] = 'e';
            target[2] = 'c';
            target[3] = '0';
            target[4] = ':';
            target[5] = ':';
            target[6] = '2';
            target[7] = '\0';
        } else if (kstrcmp(target, "dns") == 0) {
            target[0] = 'f';
            target[1] = 'e';
            target[2] = 'c';
            target[3] = '0';
            target[4] = ':';
            target[5] = ':';
            target[6] = '3';
            target[7] = '\0';
        } else if (kstrcmp(target, "self") == 0 || kstrcmp(target, "local") == 0) {
            target[0] = 'f';
            target[1] = 'e';
            target[2] = 'c';
            target[3] = '0';
            target[4] = ':';
            target[5] = ':';
            target[6] = 'f';
            target[7] = '\0';
        }

        if (target[0] == '\0') {
            monitor_write("Usage: ping6 fec0::2\n");
            monitor_write("Aliases: ping6 gateway, ping6 dns, ping6 self\n");
            monitor_write("Website hostnames still need DNS support.\n");
            return;
        }

        if (net_ping6(target)) {
            monitor_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            monitor_write("ICMPv6 echo reply received.\n");
            monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        } else {
            monitor_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            monitor_write("Ping6 failed or timed out.\n");
            monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
    }
 
    // Memory metrics reporting tool 
    else if (kstrcmp(input, "free") == 0) {
        uint32_t total = pmm_get_total_frames() * 4 / 1024;
        uint32_t used = pmm_get_used_frames() * 4 / 1024;
        uint32_t free_mem = total - used;

        monitor_write("Physical Memory Allocation Breakdown:\n");
        monitor_write("  Total MiB: ");
        kdebug_write_dec(total);
        monitor_write("\n  Used MiB : ");
        kdebug_write_dec(used);
        monitor_write("\n  Free MiB : ");
        kdebug_write_dec(free_mem);
        monitor_write("\n");
        
        serial_write("\n[ MEMORY REPORT ]\n");
        serial_write("  -> PMM Core Bitmap Initialized and Tracking Framework Active\n");
        
        // Simple condition pass to evaluate the math and silence any compiler warnings cleanly
        if (free_mem > 0 || used > 0) {
            __asm__ volatile("nop");
        }
    }
    else if (kstrcmp(input, "heap") == 0) {
        monitor_write("Kernel Heap Allocation Breakdown:\n");
        monitor_write("  Total bytes  : ");
        kdebug_write_dec(kheap_get_total_bytes());
        monitor_write("\n  Used bytes   : ");
        kdebug_write_dec(kheap_get_used_bytes());
        monitor_write("\n  Free bytes   : ");
        kdebug_write_dec(kheap_get_free_bytes());
        monitor_write("\n  Largest free : ");
        kdebug_write_dec(kheap_get_largest_free_block());
        monitor_write("\n");

        serial_write("\n[ HEAP REPORT ]\n");
        serial_write("  -> Kernel heap allocator online inside first paging window.\n");
    }
    else if (kstrcmp(input, "vm") == 0) {
        uint32_t heap_base = 0x00400000;
        monitor_write("Paging Translation Check:\n");
        monitor_write("  Heap base virtual: ");
        kdebug_write_hex(heap_base);
        monitor_write("\n  Heap base physical: ");
        kdebug_write_hex(virt_to_phys(heap_base));
        monitor_write("\n  Heap base mapped: ");
        monitor_write(is_page_mapped(heap_base) ? "yes\n" : "no\n");
    }
    else if (kstrcmp(input, "exec") == 0) {
        executable_api_t api = {
            exec_api_write_line,
            exec_api_write,
            exec_api_write_hex,
            exec_api_write_dec
        };
        uint32_t image_size =
            (uint32_t)(_binary_build_embedded_test_program_elf_end -
                       _binary_build_embedded_test_program_elf_start);
        elf_load_and_run(_binary_build_embedded_test_program_elf_start, image_size, &api);
    }
    else if (kstrcmp(input, "ls") == 0) {
        fat_file_info_t entries[32];
        uint32_t entry_count = 0;
        if (!fat_list_root(entries, 32, &entry_count)) {
            monitor_write("rootfs not mounted or unreadable.\n");
            return;
        }

        monitor_write("rootfs directory listing:\n");
        for (uint32_t i = 0; i < entry_count && i < 32; i++) {
            monitor_write("  ");
            monitor_write(entries[i].name);
            if (entries[i].is_directory) {
                monitor_write(" <DIR>");
            } else {
                monitor_write(" ");
                kdebug_write_dec(entries[i].size);
                monitor_write(" bytes");
            }
            monitor_write("\n");
        }
    }
    else if (kstrncmp(input, "cat ", 4) == 0) {
        uint8_t *file_data = 0;
        uint32_t file_size = 0;
        if (!fat_read_file(input + 4, &file_data, &file_size)) {
            monitor_write("Unable to read requested rootfs file.\n");
            return;
        }
        monitor_write("----- file begin -----\n");
        for (uint32_t i = 0; i < file_size; i++) {
            monitor_put((char)file_data[i]);
        }
        monitor_write("\n----- file end -----\n");
        kfree(file_data);
    }
    else if (kstrncmp(input, "rm ", 3) == 0) {
        if (fat_delete_file(input + 3)) {
            monitor_write("File deleted from rootfs.\n");
        } else {
            monitor_write("Failed to delete file from rootfs.\n");
        }
    }
    else if (kstrncmp(input, "write ", 6) == 0) {
        const char *args = input + 6;
        uint32_t split = 0;
        while (args[split] != '\0' && args[split] != ' ') {
            split++;
        }

        if (args[split] == '\0') {
            monitor_write("Usage: write NAME.TXT text\n");
            return;
        }

        char name[13];
        if (split >= sizeof(name)) {
            monitor_write("Filename too long for current 8.3 rootfs support.\n");
            return;
        }

        for (uint32_t i = 0; i < split; i++) {
            name[i] = args[i];
        }
        name[split] = '\0';

        const uint8_t *text = (const uint8_t *)(args + split + 1);
        uint32_t text_len = kstrlen((const char *)text);
        if (fat_write_file(name, text, text_len)) {
            monitor_write("File written to rootfs.\n");
        } else {
            monitor_write("Failed to write file to rootfs.\n");
        }
    }
    else if (kstrcmp(input, "") == 0) {
        // Return blank prompt safely if user just hits enter
    } 
    else {
        monitor_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        monitor_write("Unknown Command: ");
        monitor_write(input);
        monitor_write("\n");
        monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

void kernel_main(uint32_t mmap_entry_count, bios_e820_entry_t* mmap_entries) {
    // Safely extract the runtime link address inside the execution context
    kernel_end = (uint32_t)end;

    init_serial();
    serial_write("\n--- COM1 Serial Logging Link Online ---\n\n");

    monitor_clear();
    
    monitor_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
    monitor_write("Starting Custom Minimal Monolithic Kernel...\n");
    monitor_write("Build Target: i686-elf (32-bit Protected Mode)\n");
    monitor_write("--------------------------------------------------\n\n");
    monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    kdebug_write_line("[BOOT] Serial and VGA logging are now mirrored in lockstep.");
    kdebug_write("[BOOT] BIOS E820 entry count received: ");
    kdebug_write_dec(mmap_entry_count);
    kdebug_put('\n');
    kdebug_write("[BOOT] Kernel image end symbol resolved to ");
    kdebug_write_hex(kernel_end);
    kdebug_put('\n');
    kdebug_write("[BOOT] Early shell stack base assumed below 1 MiB and reserved by PMM.\n");

    if (mmap_entries == 0 || mmap_entry_count == 0) {
        klog_warn("BIOS E820 memory map unavailable. Physical memory manager will stay offline.");
    }

    // 1. Core CPU Segment tables
    klog_info("Initializing Global Descriptor Table (GDT)...");
    init_gdt();
    klog_ok("GDT loaded successfully. Kernel code/data segments active.");

    klog_info("Initializing Interrupt Descriptor Table (IDT)...");
    init_idt();
    klog_ok("IDT loaded. CPU Exception handlers (0-31) registered.");

    // 2. Memory Subsystem Allocation Core Initialization
    klog_info("Parsing BIOS E820 memory map and initializing Page Frame Allocator (PMM)...");
    if (mmap_entries && mmap_entry_count) {
        pmm_init(mmap_entries, mmap_entry_count, 0x8000, kernel_end);
        klog_ok("PMM Allocation Bitmap constructed. Core RAM regions initialized.");
    } else {
        klog_warn("BIOS memory map structurally null! Physical Memory Allocator offline.");
    }

    init_paging(kernel_end);
    kdebug_write("[BOOT] Paging status check complete. PMM total frames tracked: ");
    kdebug_write_dec(pmm_get_total_frames());
    kdebug_put('\n');
    kdebug_write("[BOOT] PMM used frames reserved after kernel/paging setup: ");
    kdebug_write_dec(pmm_get_used_frames());
    kdebug_put('\n');

    init_kheap(kernel_end);
    kdebug_write("[BOOT] Kernel heap total bytes available: ");
    kdebug_write_dec(kheap_get_total_bytes());
    kdebug_put('\n');
    kdebug_write("[BOOT] Kernel heap free bytes immediately after init: ");
    kdebug_write_dec(kheap_get_free_bytes());
    kdebug_put('\n');

    void *heap_probe_a = kmalloc(64);
    void *heap_probe_b = kmalloc(128);
    kdebug_write("[BOOT] Heap self-test allocation A returned ");
    kdebug_write_hex((uint32_t)heap_probe_a);
    kdebug_put('\n');
    kdebug_write("[BOOT] Heap self-test allocation B returned ");
    kdebug_write_hex((uint32_t)heap_probe_b);
    kdebug_put('\n');
    kfree(heap_probe_b);
    kfree(heap_probe_a);
    kdebug_write("[BOOT] Heap self-test complete. Free bytes after release: ");
    kdebug_write_dec(kheap_get_free_bytes());
    kdebug_put('\n');
    kdebug_write("[BOOT] Heap base translation after self-test: ");
    kdebug_write_hex(virt_to_phys(0x00400000));
    kdebug_put('\n');

    void *heap_growth_probe = kmalloc(70000);
    kdebug_write("[BOOT] Heap growth probe allocation returned ");
    kdebug_write_hex((uint32_t)heap_growth_probe);
    kdebug_put('\n');
    kfree(heap_growth_probe);
    kdebug_write("[BOOT] Heap growth probe released. Largest free block is now ");
    kdebug_write_dec(kheap_get_largest_free_block());
    kdebug_put('\n');

    executable_api_t exec_api = {
        exec_api_write_line,
        exec_api_write,
        exec_api_write_hex,
        exec_api_write_dec
    };
    uint32_t embedded_image_size =
        (uint32_t)(_binary_build_embedded_test_program_elf_end -
                   _binary_build_embedded_test_program_elf_start);
    kdebug_write("[BOOT] Embedded ELF payload byte size: ");
    kdebug_write_dec(embedded_image_size);
    kdebug_put('\n');
    elf_load_and_run(_binary_build_embedded_test_program_elf_start, embedded_image_size, &exec_api);

    ata_device_info_t rootfs_drive;
    klog_info("Probing host-backed rootfs disk on ATA primary slave...");
    if (ata_identify(ATA_DRIVE_SLAVE, &rootfs_drive)) {
        klog_ok("ATA rootfs disk identified successfully.");
        kdebug_write("    [ata] Model: ");
        kdebug_write_line(rootfs_drive.model);
        kdebug_write("    [ata] Sector count: ");
        kdebug_write_dec(rootfs_drive.sector_count);
        kdebug_put('\n');

        if (fat_mount(ATA_DRIVE_SLAVE)) {
            klog_ok("Mounted FAT rootfs from virtual ATA disk image.");

            fat_file_info_t root_entries[16];
            uint32_t root_entry_count = 0;
            if (fat_list_root(root_entries, 16, &root_entry_count)) {
                kdebug_write("[BOOT] rootfs entry count detected: ");
                kdebug_write_dec(root_entry_count);
                kdebug_put('\n');
            }

            uint8_t *readme_data = 0;
            uint32_t readme_size = 0;
            if (fat_read_file("HELLO.TXT", &readme_data, &readme_size)) {
                kdebug_write("[BOOT] Read HELLO.TXT successfully, size ");
                kdebug_write_dec(readme_size);
                kdebug_put('\n');
                kfree(readme_data);
            }

            static const uint8_t self_test_text[] = "temporary rootfs self-test";
            if (fat_write_file("TMP.TXT", self_test_text, sizeof(self_test_text) - 1)) {
                kdebug_write_line("[BOOT] Wrote TMP.TXT to rootfs successfully.");
                if (fat_delete_file("TMP.TXT")) {
                    kdebug_write_line("[BOOT] Deleted TMP.TXT from rootfs successfully.");
                } else {
                    kdebug_write_line("[BOOT] Failed to delete TMP.TXT from rootfs.");
                }
            } else {
                kdebug_write_line("[BOOT] Failed to write TMP.TXT to rootfs.");
            }
        } else {
            klog_warn("Rootfs disk detected but FAT mount failed.");
        }
    } else {
        klog_warn("No host-backed rootfs disk detected on ATA primary slave.");
    }

    // 3. Subsystem peripheral routing
    klog_info("Remapping 8259 PIC controllers...");
    klog_ok("PIC remapped. Master: IRQ 0-7 -> 0x20-0x27, Slave: IRQ 8-15 -> 0x28-0x2F.");

    klog_info("Initializing PS/2 Keyboard Driver...");
    init_keyboard();
    klog_ok("Keyboard active. IRQ 1 line unmasked.");

    klog_info("Programming 8254 Programmable Interval Timer (PIT)...");
    init_timer(100);
    klog_ok("PIT configured to Channel 0, Mode 3 (Square Wave) at 100Hz (10ms tick).");

    klog_info("Initializing PCI NIC and IPv4 networking stack...");
    if (net_init()) {
        klog_ok("rtl8139 networking online with polling RX/TX path.");
    } else {
        klog_warn("Networking stack offline. ping command will fail until NIC bring-up succeeds.");
    }

    klog_info("Masking unused IRQ lines and preparing CPU state...");
    monitor_write("    [ -> ] Executing assembly instruction: 'sti'\n");
    __asm__ volatile("sti");
    klog_ok("Hardware interrupts globally enabled on execution core.");
    if (net_ping("10.0.2.2")) {
        klog_ok("Boot-time ICMP echo to 10.0.2.2 succeeded.");
    } else {
        klog_warn("Boot-time ICMP echo to 10.0.2.2 failed.");
    }
    if (net_ping6("fec0::2")) {
        klog_ok("Boot-time ICMPv6 echo to fec0::2 succeeded.");
    } else {
        klog_warn("Boot-time ICMPv6 echo to fec0::2 failed.");
    }

    monitor_write("\n");
    monitor_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    monitor_write("==================================================\n");
    monitor_write("  BOOT SEQUENCE COMPLETE. SHELL READY.           \n");
    monitor_write("==================================================\n\n");
    monitor_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kdebug_write_line("[BOOT] Entering idle shell loop. Timer heartbeats will continue on serial.");

    char command_line[256];
    int command_index = 0;
    monitor_write("root@kernel:/# ");

    while (1) {
        net_poll();
        if (keyboard_has_char()) {
            char c = keyboard_get_char();

            if (c == '\n') {
                command_line[command_index] = '\0';
                execute_command(command_line);
                command_index = 0;
                command_line[0] = '\0';
                monitor_write("root@kernel:/# ");
            } 
            else if (c == '\b') {
                if (command_index > 0) {
                    command_index--;
                    command_line[command_index] = '\0';
                    monitor_put(c);
                }
            } 
            else {
                if (command_index < 255) {
                    command_line[command_index] = c;
                    command_index++;
                    monitor_put(c);
                }
            }
        }
        __asm__ volatile("hlt");
    }
}
