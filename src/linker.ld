ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    . = 0x8000;

    .text : {
        build/kernel_entry.o(.text)
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
        _data_end = .;
    }

    .bss : {
        *(.bss)
        _bss_end = .;
    }

    end = .;

    /DISCARD/ : {
        *(.note*)
        *(.eh_frame)
        *(.comment)
    }
}