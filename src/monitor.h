#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

// Standard VGA Text Mode Colors
#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GREY    7
#define VGA_COLOR_DARK_GREY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW        14
#define VGA_COLOR_WHITE         15

// Core Video Functions
void monitor_clear();
void monitor_put(char c);
void monitor_write(const char *string);
void monitor_set_color(uint8_t foreground, uint8_t background);

// Hardware Cursor Controls
void monitor_disable_cursor();
void monitor_update_cursor(int x, int y);

// Verbose Boot Logging Functions
void klog_ok(const char *message);
void klog_info(const char *message);
void klog_warn(const char *message);
void klog_error(const char *message);

#endif
