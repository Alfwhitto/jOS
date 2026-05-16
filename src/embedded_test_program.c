#include <stdint.h>

typedef struct executable_api {
    void (*write_line)(const char *message);
    void (*write)(const char *message);
    void (*write_hex)(uint32_t value);
    void (*write_dec)(uint32_t value);
} executable_api_t;

void _start(executable_api_t *api) {
    api->write_line("[TEST PROGRAM] Embedded ELF test payload started.");
    api->write("[TEST PROGRAM] API pointer received: ");
    api->write_hex((uint32_t)api);
    api->write("\n");
    api->write("[TEST PROGRAM] Arithmetic sanity result: ");
    api->write_dec(1337 + 29);
    api->write("\n");
    api->write_line("[TEST PROGRAM] Returning control to the kernel.");
}
