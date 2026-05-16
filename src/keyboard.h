#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Define a stable, power-of-two size for the circular input buffer
#define KEYBOARD_BUFFER_SIZE 256

// Core driver entry points
void init_keyboard();
void keyboard_callback(void *regs); // Matches your IDT register framework signature

// Shell interface polling utilities
int keyboard_has_char();
char keyboard_get_char();

#endif