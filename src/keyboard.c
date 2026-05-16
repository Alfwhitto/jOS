#include "keyboard.h"
#include "debug.h"
#include "serial.h"

// Low-level port input helper
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Low-level port output helper
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

// External registration declaration matching your IDT handler core setup
extern void register_interrupt_handler(uint8_t n, void *handler);

// Circular Buffer Core tracking structures
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static uint32_t buffer_head = 0;
static uint32_t buffer_tail = 0;
static uint8_t left_shift_down = 0;
static uint8_t right_shift_down = 0;
static uint8_t caps_lock_enabled = 0;

// Set 1 scancode table with UK-oriented ASCII mappings for the current shell.
static const char keymap_unshifted[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '^', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,   'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '@', '#', 0,   '\\','z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const char keymap_shifted[128] = {
    0,   27,  '!', '"', '#', '$', '%', '^', '&', '*', '(', ')', '_', '~', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,   'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '~', 0,   0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static int is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char translate_scancode(uint8_t scancode) {
    uint8_t shift_down = left_shift_down || right_shift_down;
    char character = shift_down ? keymap_shifted[scancode] : keymap_unshifted[scancode];

    if (!is_letter(character)) {
        return character;
    }

    if (caps_lock_enabled ^ shift_down) {
        if (character >= 'a' && character <= 'z') {
            return (char)(character - ('a' - 'A'));
        }
    } else if (character >= 'A' && character <= 'Z') {
        return (char)(character + ('a' - 'A'));
    }

    return character;
}

void init_keyboard() {
    buffer_head = 0;
    buffer_tail = 0;
    left_shift_down = 0;
    right_shift_down = 0;
    caps_lock_enabled = 0;

    // 1. Flush any unread junk data out of the controller chip status buffer
    while (inb(0x64) & 1) {
        inb(0x60);
    }

    // 2. Bind IRQ1 (Vector 33) to our callback handler
    register_interrupt_handler(33, keyboard_callback);

    // 3. Force unmask Master PIC IRQ1 line explicitly
    uint8_t master_mask = inb(0x21);
    outb(0x21, master_mask & ~(1 << 1));
    
    serial_write("[ INFO ] Keyboard hardware unmasked and bound to Vector 33.\n");
    serial_write("[ INFO ] Keyboard layout set to en_GB shell mapping (ASCII-compatible path).\n");
}

int keyboard_has_char() {
    return (buffer_head != buffer_tail);
}

char keyboard_get_char() {
    if (buffer_head == buffer_tail) {
        return 0;
    }
    char c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

void keyboard_callback(void *regs) {
    (void)regs;

    uint8_t scancode = inb(0x60);

    if (scancode == 0x2A) {
        left_shift_down = 1;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0x36) {
        right_shift_down = 1;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0xAA) {
        left_shift_down = 0;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0xB6) {
        right_shift_down = 0;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0x3A) {
        caps_lock_enabled ^= 1;
        outb(0x20, 0x20);
        return;
    }

    // Filter out key-release break-codes (bit 7 set)
    if (!(scancode & 0x80)) {
        char ascii = translate_scancode(scancode);
        
        if (ascii != 0) {
            uint32_t next_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
            
            if (next_head != buffer_tail) {
                keyboard_buffer[buffer_head] = ascii;
                buffer_head = next_head;
            } else {
                kdebug_write_line("[ WARN ] Keyboard input ring buffer overrun!");
            }
        }
    }

    // Send EOI to Master PIC (Port 0x20)
    outb(0x20, 0x20);
}
