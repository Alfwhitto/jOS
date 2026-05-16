#include "timer.h"
#include "serial.h" // Swapping monitor header out for serial control

static uint32_t tick = 0;

static void timer_callback(registers_t *regs) {
    (void)regs; // Silences the unused parameter warning safely
    tick++;
    
    // Every 100 ticks (approx 1 second at 100Hz), dump state to serial monitor
    if (tick % 100 == 0) {
        serial_write("[ HEARTBEAT ] 100 system ticks passed safely.\n");
    }
}

void init_timer(uint32_t frequency) {
    // Standard PIT Frequency Calculation Formula logic
    // (1193182 Hz / frequency)
    uint32_t divisor = 1193182 / frequency;

    // Send the operational command byte to the PIT Mode Command Register (Port 0x43)
    // 0x36 sets: Square Wave Mode, Channel 0, Access LSB then MSB
    __asm__ volatile("outb %%al, %%dx" : : "a"(0x36), "d"(0x43));

    // Split the 16-bit divisor into lower and upper bytes
    uint8_t l = (uint8_t)(divisor & 0xFF);
    uint8_t h = (uint8_t)((divisor >> 8) & 0xFF);

    // Send the frequency divisor bytes to PIT Channel 0 Data Port (Port 0x40)
    __asm__ volatile("outb %%al, %%dx" : : "a"(l), "d"(0x40));
    __asm__ volatile("outb %%al, %%dx" : : "a"(h), "d"(0x40));

    // Register our timer callback routine with Interrupt Vector 32 (IRQ 0)
    register_interrupt_handler(32, timer_callback);
}

uint32_t timer_get_ticks(void) {
    return tick;
}
