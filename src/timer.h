#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "idt.h"

void init_timer(uint32_t frequency);
uint32_t timer_get_ticks(void);

#endif
