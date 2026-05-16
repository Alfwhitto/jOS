#include "debug.h"
#include "monitor.h"
#include "serial.h"

static void write_unsigned(uint32_t value, uint32_t base) {
    char buffer[16];
    int index = 0;

    if (value == 0) {
        kdebug_put('0');
        return;
    }

    while (value > 0) {
        uint32_t digit = value % base;
        buffer[index++] = (digit < 10) ? (char)('0' + digit)
                                       : (char)('A' + (digit - 10));
        value /= base;
    }

    while (index > 0) {
        kdebug_put(buffer[--index]);
    }
}

void kdebug_put(char c) {
    monitor_put(c);
    if (c == '\n') {
        serial_put('\r');
    }
    serial_put(c);
}

void kdebug_write(const char *string) {
    while (*string) {
        kdebug_put(*string++);
    }
}

void kdebug_write_line(const char *string) {
    kdebug_write(string);
    kdebug_put('\n');
}

void kdebug_write_hex(uint32_t value) {
    kdebug_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xF;
        kdebug_put((digit < 10) ? (char)('0' + digit)
                                : (char)('A' + (digit - 10)));
    }
}

void kdebug_write_dec(uint32_t value) {
    write_unsigned(value, 10);
}
