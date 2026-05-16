#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

void kdebug_put(char c);
void kdebug_write(const char *string);
void kdebug_write_line(const char *string);
void kdebug_write_hex(uint32_t value);
void kdebug_write_dec(uint32_t value);

#endif
