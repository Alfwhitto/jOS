#ifndef ELF_H
#define ELF_H

#include <stdint.h>

typedef struct elf32_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'

#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define EM_386 3
#define ET_EXEC 2

#define PT_LOAD 1

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct executable_api {
    void (*write_line)(const char *message);
    void (*write)(const char *message);
    void (*write_hex)(uint32_t value);
    void (*write_dec)(uint32_t value);
} executable_api_t;

int elf_load_and_run(const uint8_t *image, uint32_t image_size, executable_api_t *api);

#endif
