org 0x7C00
bits 16

LOAD_START_LBA equ 1
KERNEL_BUFFER_SEG equ 0x07E0
RELOCATED_MBR_OFF equ 0x0600

relocated_start equ RELOCATED_MBR_OFF + (mbr_main - $$)

%include "build/kernel_sectors.inc"

TOTAL_IMAGE_SECTORS equ KERNEL_SECTORS + 1

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

    call init_serial
    mov al, 'M'
    call serial_put

    mov word [load_segment], KERNEL_BUFFER_SEG
    mov word [sectors_remaining], TOTAL_IMAGE_SECTORS
    mov dword [current_lba], LOAD_START_LBA

.load_loop:
    cmp word [sectors_remaining], 0
    je .load_done

    mov word [disk_address_packet + 2], 1
    mov word [disk_address_packet + 4], 0
    mov ax, [load_segment]
    mov word [disk_address_packet + 6], ax
    mov ax, [current_lba]
    mov word [disk_address_packet + 8], ax
    mov ax, [current_lba + 2]
    mov word [disk_address_packet + 10], ax

    mov si, disk_address_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_error

    add word [current_lba], 1
    adc word [current_lba + 2], 0
    add word [load_segment], 0x20
    sub word [sectors_remaining], 1
    jmp .load_loop

.load_done:
    mov al, 'L'
    call serial_put

    cld
    mov si, 0x7E00
    mov di, 0x7C00
    mov cx, 256
    rep movsw

    mov dl, [boot_drive]
    mov al, 'J'
    call serial_put
    jmp 0x0000:0x7C00

disk_error:
    mov al, 'E'
    call serial_put
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    jmp $

init_serial:
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x01
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    ret

serial_put:
    push dx
    push bx
    mov bl, al
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    mov al, bl
    mov dx, 0x3F8
    out dx, al
    pop bx
    pop dx
    ret

boot_drive: db 0
sectors_remaining: dw 0
load_segment: dw 0
current_lba: dd 0
align 16
disk_address_packet:
    db 0x10
    db 0x00
    dw 0x0001
    dw 0x7C00
    dw 0x0000
    dd LOAD_START_LBA
    dd 0x00000000

times 446-($-$$) db 0

; Single active partition covering the rest of the disk. The second-stage boot
; sector lives at its first LBA, which keeps VMware happy with a conventional
; hard-disk layout while letting our custom loader take over immediately.
db 0x80
db 0x00, 0x02, 0x00
db 0x7F
db 0xFE, 0xFF, 0xFF
dd 0x00000001
dd 0x0001FFFF

times 16*3 db 0
dw 0xAA55
