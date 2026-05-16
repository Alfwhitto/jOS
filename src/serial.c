#include "serial.h"

// Low-level helper functions to interface with CPU I/O space
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Check if the serial transmit buffer is empty and ready to send data
static int is_transmit_empty() {
    return inb(COM1_PORT + 5) & 0x20;
}

void init_serial() {
    outb(COM1_PORT + 1, 0x00);    // Disable all interrupts from UART
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x01);    // Set divisor to 1 (lo byte): 115200 baud
    outb(COM1_PORT + 1, 0x00);    //                  (hi byte)
    outb(COM1_PORT + 3, 0x03);    // 8 data bits, no parity, one stop bit (8N1)
    outb(COM1_PORT + 4, 0x0B);    // Enable FIFO, turn on DTR, RTS, and OUT2
}

void serial_put(char c) {
    // Spinlock until the transmit port buffer indicates it's clear
    while (is_transmit_empty() == 0);
    
    outb(COM1_PORT, c);
}

void serial_write(const char *string) {
    for (int i = 0; string[i] != '\0'; i++) {
        // If your string has a newline, serial consoles usually need a carriage return (\r) too
        if (string[i] == '\n') {
            serial_put('\r');
        }
        serial_put(string[i]);
    }
}