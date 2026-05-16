#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

// Standard I/O Port for COM1 Serial Port
#define COM1_PORT 0x3F8

// Driver Initialization and Core Writing Hooks
void init_serial();
void serial_put(char c);
void serial_write(const char *string);

#endif