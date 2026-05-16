#include "monitor.h"
#include "serial.h"

uint16_t *video_memory = (uint16_t *)0xB8000;
uint8_t cursor_x = 0;
uint8_t cursor_y = 0;
uint8_t attribute_byte = (0 << 4) | 15; // Black background, White text default

// Low-level port output helper for VGA registers
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Tells the hardware CRT Controller chip to disable the blinking cursor completely
void monitor_disable_cursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

// Moves the hardware blinking text cursor to match our software coordinates
void monitor_update_cursor(int x, int y) {
    uint16_t pos = y * 80 + x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

// Scrolls the text screen up by one line when reaching the bottom boundary
static void scroll() {
    uint16_t blank = 0x20 | (attribute_byte << 8);

    if (cursor_y >= 25) {
        for (int i = 0 * 80; i < 24 * 80; i++) {
            video_memory[i] = video_memory[i + 80];
        }

        for (int i = 24 * 80; i < 25 * 80; i++) {
            video_memory[i] = blank;
        }

        cursor_y = 24;
    }
}

void monitor_clear() {
    uint16_t blank = 0x20 | (attribute_byte << 8);
    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i] = blank;
    }
    cursor_x = 0;
    cursor_y = 0;
    
    // Sync the hardware cursor back to the origin
    monitor_update_cursor(cursor_x, cursor_y);
}

void monitor_set_color(uint8_t foreground, uint8_t background) {
    attribute_byte = (background << 4) | (foreground & 0x0F);
}

void monitor_put(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } 
    else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            uint16_t attribute = attribute_byte << 8;
            uint16_t *location = video_memory + (cursor_y * 80 + cursor_x);
            *location = ' ' | attribute;
        }
    } 
    else if (c >= ' ') {
        uint16_t attribute = attribute_byte << 8;
        uint16_t *location = video_memory + (cursor_y * 80 + cursor_x);
        *location = c | attribute;
        cursor_x++;
    }

    if (cursor_x >= 80) {
        cursor_x = 0;
        cursor_y++;
    }

    scroll();

    // CRITICAL: Force the blinking hardware cursor to stay locked to our writing path
    monitor_update_cursor(cursor_x, cursor_y);
}

void monitor_write(const char *string) {
    int i = 0;
    while (string[i]) {
        monitor_put(string[i]);
        i++;
    }
}

static void klog_status(const char *status, uint8_t color) {
    monitor_write("[ ");
    uint8_t original_attribute = attribute_byte;
    monitor_set_color(color, VGA_COLOR_BLACK);
    monitor_write(status);
    attribute_byte = original_attribute;
    monitor_write(" ] ");
}

void klog_ok(const char *message) {
    klog_status(" OK ", VGA_COLOR_LIGHT_GREEN);
    monitor_write(message);
    monitor_write("\n");
    serial_write("[  OK  ] ");
    serial_write(message);
    serial_write("\n");
}

void klog_info(const char *message) {
    klog_status("INFO", VGA_COLOR_LIGHT_CYAN);
    monitor_write(message);
    monitor_write("\n");
    serial_write("[ INFO ] ");
    serial_write(message);
    serial_write("\n");
}

void klog_warn(const char *message) {
    klog_status("WARN", VGA_COLOR_YELLOW);
    monitor_write(message);
    monitor_write("\n");
    serial_write("[ WARN ] ");
    serial_write(message);
    serial_write("\n");
}

void klog_error(const char *message) {
    klog_status("ERR!", VGA_COLOR_LIGHT_RED);
    monitor_write(message);
    monitor_write("\n");
    serial_write("[ ERR! ] ");
    serial_write(message);
    serial_write("\n");
}
