ENTRY(_start)

SECTIONS
{
    . = 0x00500000;

    .text : {
        *(.text)
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}
