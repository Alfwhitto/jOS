#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#include "boot_video.h"

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
void monitor_init(const boot_video_info_t *video_info, void *framebuffer_virt);
void monitor_clear();
void monitor_put(char c);
void monitor_write(const char *string);
void monitor_write_dec(uint32_t value);
void monitor_write_hex(uint32_t value);
void monitor_set_color(uint8_t foreground, uint8_t background);
void monitor_draw_pixel(int x, int y, uint8_t color);
void monitor_draw_line(int x0, int y0, int x1, int y1, uint8_t color);
void monitor_draw_rect(int x, int y, int width, int height, uint8_t color);
void monitor_fill_rect(int x, int y, int width, int height, uint8_t color);
uint32_t monitor_get_width(void);
uint32_t monitor_get_height(void);

// Hardware Cursor Controls
void monitor_disable_cursor();
void monitor_update_cursor(int x, int y);

// Verbose Boot Logging Functions
void klog_ok(const char *message);
void klog_info(const char *message);
void klog_warn(const char *message);
void klog_error(const char *message);

#endif
