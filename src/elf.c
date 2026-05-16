#include "elf.h"
#include "debug.h"
#include "monitor.h"
#include "paging.h"

typedef void (*loaded_program_entry_t)(executable_api_t *api);

static uint32_t page_align_down(uint32_t value) {
    return value & 0xFFFFF000;
}

static uint32_t page_align_up(uint32_t value) {
    return (value + PAGING_PAGE_SIZE - 1) & ~(PAGING_PAGE_SIZE - 1);
}

static int elf_validate(const uint8_t *image, uint32_t image_size, const elf32_ehdr_t **header_out) {
    if (image_size < sizeof(elf32_ehdr_t)) {
        klog_error("ELF image too small to contain a valid header.");
        return 0;
    }

    const elf32_ehdr_t *header = (const elf32_ehdr_t *)image;
    if (header->e_ident[0] != ELF_MAGIC_0 ||
        header->e_ident[1] != ELF_MAGIC_1 ||
        header->e_ident[2] != ELF_MAGIC_2 ||
        header->e_ident[3] != ELF_MAGIC_3) {
        klog_error("Embedded image is not a valid ELF file.");
        return 0;
    }

    if (header->e_ident[4] != ELFCLASS32 || header->e_ident[5] != ELFDATA2LSB) {
        klog_error("ELF loader only supports 32-bit little-endian executables.");
        return 0;
    }

    if (header->e_machine != EM_386 || header->e_type != ET_EXEC ||
        header->e_version != EV_CURRENT) {
        klog_error("ELF image has unsupported machine, type, or version.");
        return 0;
    }

    *header_out = header;
    return 1;
}

static int map_segment_memory(const elf32_phdr_t *program_header) {
    uint32_t start = page_align_down(program_header->p_vaddr);
    uint32_t end = page_align_up(program_header->p_vaddr + program_header->p_memsz);
    uint32_t flags = PAGING_FLAG_PRESENT;

    if (program_header->p_flags & PF_W) {
        flags |= PAGING_FLAG_WRITABLE;
    }

    for (uint32_t address = start; address < end; address += PAGING_PAGE_SIZE) {
        if (!alloc_page(address, flags)) {
            return 0;
        }
    }

    return 1;
}

int elf_load_and_run(const uint8_t *image, uint32_t image_size, executable_api_t *api) {
    const elf32_ehdr_t *header = 0;
    if (!elf_validate(image, image_size, &header)) {
        return 0;
    }

    klog_info("Loading embedded ELF executable...");
    kdebug_write("    [elf] Image size: ");
    kdebug_write_dec(image_size);
    kdebug_put('\n');
    kdebug_write("    [elf] Program header count: ");
    kdebug_write_dec(header->e_phnum);
    kdebug_put('\n');
    kdebug_write("    [elf] Entry point: ");
    kdebug_write_hex(header->e_entry);
    kdebug_put('\n');

    if (header->e_phoff + ((uint32_t)header->e_phnum * header->e_phentsize) > image_size) {
        klog_error("ELF program header table extends beyond embedded image.");
        return 0;
    }

    const elf32_phdr_t *program_headers = (const elf32_phdr_t *)(image + header->e_phoff);

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const elf32_phdr_t *program_header =
            (const elf32_phdr_t *)((const uint8_t *)program_headers + (i * header->e_phentsize));

        if (program_header->p_type != PT_LOAD) {
            continue;
        }

        if (program_header->p_offset + program_header->p_filesz > image_size) {
            klog_error("ELF segment payload extends beyond embedded image.");
            return 0;
        }

        kdebug_write("    [elf] PT_LOAD #");
        kdebug_write_dec(i);
        kdebug_write(": vaddr=");
        kdebug_write_hex(program_header->p_vaddr);
        kdebug_write(" filesz=");
        kdebug_write_dec(program_header->p_filesz);
        kdebug_write(" memsz=");
        kdebug_write_dec(program_header->p_memsz);
        kdebug_put('\n');

        if (!map_segment_memory(program_header)) {
            klog_error("ELF loader failed while mapping segment pages.");
            return 0;
        }

        uint8_t *destination = (uint8_t *)program_header->p_vaddr;
        const uint8_t *source = image + program_header->p_offset;

        for (uint32_t byte_index = 0; byte_index < program_header->p_filesz; byte_index++) {
            destination[byte_index] = source[byte_index];
        }

        for (uint32_t byte_index = program_header->p_filesz;
             byte_index < program_header->p_memsz; byte_index++) {
            destination[byte_index] = 0;
        }
    }

    loaded_program_entry_t entry = (loaded_program_entry_t)header->e_entry;
    klog_ok("ELF image mapped and copied. Jumping to embedded test program entry.");
    entry(api);
    klog_ok("Embedded test program returned control to the kernel.");
    return 1;
}
