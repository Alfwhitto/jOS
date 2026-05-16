org 0x7C00
bits 16

KERNEL_OFFSET equ 0x8000
KERNEL_LOAD_SEG equ (KERNEL_OFFSET >> 4)
KERNEL_LOAD_OFF equ (KERNEL_OFFSET & 0x000F)
DISK_HEADS equ 16
DISK_SECTORS_PER_TRACK equ 63
MMAP_COUNT_ADDR equ 0x1000   ; Relocated away from BIOS scratch space
MMAP_DATA_ADDR  equ 0x1004   ; Relocated away from BIOS scratch space

%ifndef KERNEL_START_LBA
KERNEL_START_LBA equ 1
%endif

%include "build/kernel_sectors.inc"

main:
    mov [BOOT_DRIVE], dl ; Save boot drive number safely in memory

    call init_serial
    mov al, 'S'
    call serial_put

    ; Safe stack setup in 16-bit mode
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Clear the screen to ensure a clean text page coordinate state
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    ; --- Detect Memory via BIOS INT 0x15, E820 ---
    call detect_memory

    ; QEMU usually leaves A20 enabled already; VMware and real BIOSes are more
    ; variable, so force it on before paging or high-memory frame use.
    call enable_a20

%ifndef KERNEL_PRELOADED
    ; Read the post-boot image one sector at a time using CHS. The VMware HDD
    ; path has been much happier with classic BIOS reads than large EDD
    ; transfers, and this kernel still fits comfortably in the early cylinders.
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov word [load_segment], KERNEL_LOAD_SEG
    mov word [sectors_remaining], KERNEL_SECTORS
    mov dword [current_lba], KERNEL_START_LBA

.load_kernel_loop:
    mov ax, [sectors_remaining]
    test ax, ax
    jz .kernel_loaded

    mov ax, [current_lba]
    xor dx, dx
    mov bx, DISK_SECTORS_PER_TRACK
    div bx
    mov cl, dl
    inc cl

    xor dx, dx
    mov bx, DISK_HEADS
    div bx

    mov ch, al
    mov bl, ah
    and bl, 0x03
    shl bl, 6
    or cl, bl
    mov dh, dl
    mov dl, [BOOT_DRIVE]
    mov ax, [load_segment]
    mov es, ax
    mov bx, KERNEL_LOAD_OFF
    mov al, 'B'
    call serial_put
    mov ax, 0x0201
    int 0x13
    jc disk_error

    mov al, 'K'
    call serial_put

    add word [current_lba], 1
    add word [load_segment], 0x20
    sub word [sectors_remaining], 1
    jmp .load_kernel_loop

.kernel_loaded:
%endif

    ; Basic sanity check: ensure the first byte of the loaded kernel
    ; at 0x8000 is non-zero. If it is zero, treat as disk failure.
    mov si, 0x8000
    mov al, [si]
    cmp al, 0
    je disk_error

    ; Transition to 32-bit Protected Mode
    ; Verify again that the memory at 0x8000 is populated before switching
    ; to protected mode (catch any corruption between load and handoff).
    mov al, [0x8000]
    cmp al, 0
    je disk_error

    cli
    lgdt [gdt_descriptor]
    
    mov eax, cr0
    or eax, 0x1         ; Enable Protected Mode bit
    mov cr0, eax

    ; Far jump to flush the 16-bit prefetch cache pipeline
    jmp CODE_SEG:init_32bit

; --- BIOS E820 Memory Detection Routine ---
detect_memory:
    pusha
    xor ebx, ebx            ; EBX must start at 0 for E820
    mov di, MMAP_DATA_ADDR  ; Destination buffer pointer for memory map entries
    xor bp, bp              ; Keep track of entries count in BP

.mmap_loop:
    mov edx, 0x534D4150    ; Magic number 'SMAP'
    mov eax, 0xE820
    mov ecx, 24             ; Request 24 bytes
    int 0x15
    jc .mmap_done           ; If carry flag set, detection is finished

    cmp eax, 0x534D4150     ; On success, EAX is reset to 'SMAP'
    jne .mmap_done

    test ecx, ecx           ; If ecx is 0, we got an empty entry
    jz .mmap_skip

    inc bp                  ; Valid entry! Increment entry count tracker
    add di, 24              ; Move destination pointer forward to next slot

.mmap_skip:
    test ebx, ebx           ; If EBX returns to 0, the list is complete
    jz .mmap_done
    jmp .mmap_loop

.mmap_done:
    mov [MMAP_COUNT_ADDR], bp ; Save the total entry count to memory
    popa
    ret

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

enable_a20:
    in al, 0x92
    test al, 0x02
    jnz .done
    or al, 0x02
    and al, 0xFE
    out 0x92, al
.done:
    ret

bits 32
init_32bit:
    ; Crucial: Point all data descriptors to our 32-bit Data Selector
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up a safe 32-bit execution stack
    mov ebp, 0x90000    
    mov esp, ebp

    ; Hand execution over to our C entry bridge!
    jmp KERNEL_OFFSET   

disk_error:
    mov al, 'D'
    call serial_put
    ; Print red failure text directly to VGA video memory (0xB8000)
    mov dword [0xB8000], 0x4F524F45 ; "E R" (Red background)
    mov dword [0xB8004], 0x4F524F52 ; "R O"
    jmp $

; Global Descriptor Table (GDT) Layout
gdt_start:
gdt_null:
    dd 0x0, 0x0
gdt_code:
    dw 0xffff, 0x0, 0x9a00, 0x00cf
gdt_data:
    dw 0xffff, 0x0, 0x9200, 0x00cf
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

BOOT_DRIVE: db 0
sectors_remaining: dw 0
load_segment: dw 0
current_lba: dd 0

times 510-($-$$) db 0
dw 0xAA55
