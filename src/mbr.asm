org 0x7C00
bits 16

LOAD_START_LBA equ 1
STAGE2_SEG equ 0x07C0
KERNEL_SEG equ 0x0800
RELOCATED_MBR_OFF equ 0x0600

relocated_start equ RELOCATED_MBR_OFF + (mbr_main - $$)

%include "build/kernel_sectors.inc"
%include "build/stage2_sectors.inc"

KERNEL_LOAD_LBA equ LOAD_START_LBA + STAGE2_SECTORS

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    cld
    mov si, 0x7C00
    mov di, RELOCATED_MBR_OFF
    mov cx, 256
    rep movsw

    jmp 0x0000:relocated_start

mbr_main:
    sti
    mov [boot_drive], dl

    mov word [load_segment], STAGE2_SEG
    mov word [sectors_remaining], STAGE2_SECTORS
    mov dword [current_lba], LOAD_START_LBA
    call load_sector_range

    mov word [load_segment], KERNEL_SEG
    mov word [sectors_remaining], KERNEL_SECTORS
    mov dword [current_lba], KERNEL_LOAD_LBA
    call load_sector_range

    mov dl, [boot_drive]
    jmp 0x0000:0x7C00

load_sector_range:
.load_loop:
    cmp word [sectors_remaining], 0
    je .load_done

    mov ax, [load_segment]
    mov [disk_address_packet + 6], ax
    mov eax, [current_lba]
    mov [disk_address_packet + 8], eax
    mov dword [disk_address_packet + 12], 0
    mov dl, [boot_drive]
    mov si, disk_address_packet
    mov ah, 0x42
    int 0x13
    jc disk_error

    add word [current_lba], 1
    adc word [current_lba + 2], 0
    add word [load_segment], 0x20
    sub word [sectors_remaining], 1
    jmp .load_loop

.load_done:
    ret

disk_error:
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    jmp $

boot_drive: db 0
sectors_remaining: dw 0
load_segment: dw 0
current_lba: dd 0
disk_address_packet:
    db 0x10
    db 0x00
    dw 0x0001
    dw 0x0000
    dw 0x0000
    dd 0x00000000
    dd 0x00000000

times 446-($-$$) db 0

; Single active partition covering the rest of the disk. The second-stage boot
; sector lives at its first LBA, which keeps the disk layout conventional while
; letting our own stage-2 loader take over immediately.
db 0x80
db 0x00, 0x02, 0x00
db 0x7F
db 0xFE, 0xFF, 0xFF
dd 0x00000001
dd 0x0001FFFF

times 16*3 db 0
dw 0xAA55
