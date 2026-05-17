#include "monitor.h"

#include "font8x8_basic.h"
#include "serial.h"

#define FONT_WIDTH 8
#define FONT_HEIGHT 8
#define MAX_TEXT_COLS 160
#define MAX_TEXT_ROWS 128

static uint8_t *framebuffer = 0;
static uint32_t framebuffer_width = 0;
static uint32_t framebuffer_height = 0;
static uint32_t framebuffer_pitch = 0;
static uint32_t framebuffer_bpp = 0;
static uint32_t text_cols = 0;
static uint32_t text_rows = 0;
static uint8_t monitor_ready = 0;

static uint8_t text_chars[MAX_TEXT_ROWS][MAX_TEXT_COLS];
static uint8_t text_attributes[MAX_TEXT_ROWS][MAX_TEXT_COLS];
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint8_t foreground_color = VGA_COLOR_WHITE;
static uint8_t background_color = VGA_COLOR_BLACK;

static const uint32_t rgb_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static uint8_t make_attribute(uint8_t foreground, uint8_t background) {
    return (uint8_t)(((background & 0x0F) << 4) | (foreground & 0x0F));
}

static uint8_t attribute_foreground(uint8_t attribute) {
    return attribute & 0x0F;
}

static uint8_t attribute_background(uint8_t attribute) {
    return (attribute >> 4) & 0x0F;
}

static int abs_int(int value) {
    return value < 0 ? -value : value;
}

static void monitor_write_unsigned(uint32_t value, uint32_t base) {
    char buffer[16];
    int index = 0;

    if (value == 0) {
        monitor_put('0');
        return;
    }

    while (value > 0) {
        uint32_t digit = value % base;
        buffer[index++] = (digit < 10) ? (char)('0' + digit)
                                       : (char)('A' + (digit - 10));
        value /= base;
    }

    while (index > 0) {
        monitor_put(buffer[--index]);
    }
}

static uint32_t pack_rgb(uint8_t color_index) {
    if (framebuffer_bpp == 8) {
        return color_index & 0x0F;
    }

    uint32_t rgb = rgb_palette[color_index & 0x0F];
    return ((rgb & 0x0000FFu) << 16) | (rgb & 0x00FF00u) | ((rgb & 0xFF0000u) >> 16);
}

static void write_pixel_native(uint32_t x, uint32_t y, uint32_t pixel) {
    uint8_t *location = framebuffer + (y * framebuffer_pitch) + (x * (framebuffer_bpp / 8));

    if (framebuffer_bpp == 8) {
        *location = (uint8_t)pixel;
    } else if (framebuffer_bpp == 32) {
        *((uint32_t *)location) = pixel;
    } else if (framebuffer_bpp == 24) {
        location[0] = (uint8_t)(pixel & 0xFF);
        location[1] = (uint8_t)((pixel >> 8) & 0xFF);
        location[2] = (uint8_t)((pixel >> 16) & 0xFF);
    }
}

static void draw_character_cell(uint32_t x, uint32_t y, char c, uint8_t attribute) {
    uint32_t fg = pack_rgb(attribute_foreground(attribute));
    uint32_t bg = pack_rgb(attribute_background(attribute));
    const uint8_t *glyph = font8x8_basic[(uint8_t)c & 0x7F];
    uint32_t pixel_x = x * FONT_WIDTH;
    uint32_t pixel_y = y * FONT_HEIGHT;

    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < FONT_WIDTH; col++) {
            uint32_t color = (bits & (1u << col)) ? fg : bg;
            write_pixel_native(pixel_x + (FONT_WIDTH - 1 - col), pixel_y + row, color);
        }
    }
}

static void fill_rect_native(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t pixel) {
    for (uint32_t py = 0; py < height; py++) {
        for (uint32_t px = 0; px < width; px++) {
            write_pixel_native(x + px, y + py, pixel);
        }
    }
}

static void copy_rect_up_native(uint32_t rows) {
    uint32_t bytes_per_pixel = framebuffer_bpp / 8;
    uint32_t bytes_to_copy = framebuffer_pitch * (framebuffer_height - rows);
    uint8_t *dest = framebuffer;
    uint8_t *src = framebuffer + (rows * framebuffer_pitch);

    if (bytes_per_pixel == 0 || rows == 0 || rows >= framebuffer_height) {
        return;
    }

    for (uint32_t i = 0; i < bytes_to_copy; i++) {
        dest[i] = src[i];
    }
}

static void draw_cursor(int visible) {
    if (!monitor_ready || cursor_x >= text_cols || cursor_y >= text_rows) {
        return;
    }

    uint8_t attribute = text_attributes[cursor_y][cursor_x];
    uint32_t color = visible
        ? pack_rgb(attribute_foreground(attribute))
        : pack_rgb(attribute_background(attribute));

    fill_rect_native(cursor_x * FONT_WIDTH,
                     (cursor_y * FONT_HEIGHT) + (FONT_HEIGHT - 2),
                     FONT_WIDTH,
                     2,
                     color);
}

static void render_full_console(void) {
    if (!monitor_ready) {
        return;
    }

    for (uint32_t y = 0; y < text_rows; y++) {
        for (uint32_t x = 0; x < text_cols; x++) {
            draw_character_cell(x, y, (char)text_chars[y][x], text_attributes[y][x]);
        }
    }

    draw_cursor(1);
}

static void scroll(void) {
    if (cursor_y < text_rows) {
        return;
    }

    for (uint32_t y = 1; y < text_rows; y++) {
        for (uint32_t x = 0; x < text_cols; x++) {
            text_chars[y - 1][x] = text_chars[y][x];
            text_attributes[y - 1][x] = text_attributes[y][x];
        }
    }

    for (uint32_t x = 0; x < text_cols; x++) {
        text_chars[text_rows - 1][x] = ' ';
        text_attributes[text_rows - 1][x] = make_attribute(foreground_color, background_color);
    }

    cursor_y = text_rows - 1;
    copy_rect_up_native(FONT_HEIGHT);
    fill_rect_native(0,
                     framebuffer_height - FONT_HEIGHT,
                     framebuffer_width,
                     FONT_HEIGHT,
                     pack_rgb(background_color));
}

void monitor_init(const boot_video_info_t *video_info, void *framebuffer_virt) {
    framebuffer = (uint8_t *)framebuffer_virt;
    framebuffer_width = video_info->width;
    framebuffer_height = video_info->height;
    framebuffer_pitch = video_info->pitch;
    framebuffer_bpp = video_info->bpp;
    text_cols = framebuffer_width / FONT_WIDTH;
    text_rows = framebuffer_height / FONT_HEIGHT;

    if (text_cols > MAX_TEXT_COLS) {
        text_cols = MAX_TEXT_COLS;
    }
    if (text_rows > MAX_TEXT_ROWS) {
        text_rows = MAX_TEXT_ROWS;
    }

    monitor_ready = 1;
    monitor_clear();
}

void monitor_disable_cursor() {
}

void monitor_update_cursor(int x, int y) {
    (void)x;
    (void)y;
}

void monitor_clear() {
    if (!monitor_ready) {
        return;
    }

    uint8_t attribute = make_attribute(foreground_color, background_color);
    fill_rect_native(0, 0, framebuffer_width, framebuffer_height, pack_rgb(background_color));

    for (uint32_t y = 0; y < text_rows; y++) {
        for (uint32_t x = 0; x < text_cols; x++) {
            text_chars[y][x] = ' ';
            text_attributes[y][x] = attribute;
        }
    }

    cursor_x = 0;
    cursor_y = 0;
    render_full_console();
}

void monitor_set_color(uint8_t foreground, uint8_t background) {
    foreground_color = foreground & 0x0F;
    background_color = background & 0x0F;
}

void monitor_draw_pixel(int x, int y, uint8_t color) {
    if (!monitor_ready || x < 0 || y < 0 ||
        (uint32_t)x >= framebuffer_width || (uint32_t)y >= framebuffer_height) {
        return;
    }

    write_pixel_native((uint32_t)x, (uint32_t)y, pack_rgb(color));
}

void monitor_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = abs_int(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs_int(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (1) {
        monitor_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }

        int doubled_error = error * 2;
        if (doubled_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void monitor_draw_rect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    monitor_draw_line(x, y, x + width - 1, y, color);
    monitor_draw_line(x, y, x, y + height - 1, color);
    monitor_draw_line(x + width - 1, y, x + width - 1, y + height - 1, color);
    monitor_draw_line(x, y + height - 1, x + width - 1, y + height - 1, color);
}

void monitor_fill_rect(int x, int y, int width, int height, uint8_t color) {
    if (!monitor_ready || width <= 0 || height <= 0) {
        return;
    }

    int start_x = x < 0 ? 0 : x;
    int start_y = y < 0 ? 0 : y;
    int end_x = x + width;
    int end_y = y + height;

    if (start_x >= (int)framebuffer_width || start_y >= (int)framebuffer_height) {
        return;
    }
    if (end_x > (int)framebuffer_width) {
        end_x = (int)framebuffer_width;
    }
    if (end_y > (int)framebuffer_height) {
        end_y = (int)framebuffer_height;
    }

    if (start_x >= end_x || start_y >= end_y) {
        return;
    }

    fill_rect_native((uint32_t)start_x,
                     (uint32_t)start_y,
                     (uint32_t)(end_x - start_x),
                     (uint32_t)(end_y - start_y),
                     pack_rgb(color));
}

uint32_t monitor_get_width(void) {
    return framebuffer_width;
}

uint32_t monitor_get_height(void) {
    return framebuffer_height;
}

void monitor_put(char c) {
    uint8_t attribute = make_attribute(foreground_color, background_color);

    if (!monitor_ready) {
        return;
    }

    draw_cursor(0);

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = text_cols - 1;
        }

        text_chars[cursor_y][cursor_x] = ' ';
        text_attributes[cursor_y][cursor_x] = attribute;
        draw_character_cell(cursor_x, cursor_y, ' ', attribute);
    } else if (c >= ' ') {
        text_chars[cursor_y][cursor_x] = (uint8_t)c;
        text_attributes[cursor_y][cursor_x] = attribute;
        draw_character_cell(cursor_x, cursor_y, c, attribute);
        cursor_x++;
    }

    if (cursor_x >= text_cols) {
        cursor_x = 0;
        cursor_y++;
    }

    scroll();
    draw_cursor(1);
}

void monitor_write(const char *string) {
    while (*string) {
        monitor_put(*string++);
    }
}

void monitor_write_dec(uint32_t value) {
    monitor_write_unsigned(value, 10);
}

void monitor_write_hex(uint32_t value) {
    monitor_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xF;
        monitor_put((digit < 10) ? (char)('0' + digit)
                                 : (char)('A' + (digit - 10)));
    }
}

static void klog_status(const char *status, uint8_t color) {
    uint8_t original_foreground = foreground_color;
    uint8_t original_background = background_color;

    monitor_write("[ ");
    monitor_set_color(color, VGA_COLOR_BLACK);
    monitor_write(status);
    monitor_set_color(original_foreground, original_background);
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
