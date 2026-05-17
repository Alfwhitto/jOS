bits 32

section .text
global _start
global gdt_flush
global idt_flush

extern kernel_main
extern isr_handler
extern irq_handler

; --- EXECUTION ENTRY POINT ---
_start:
    mov esp, stack_top

    ; Pass parameters tracking to kernel_main (Right-to-Left)
    push dword 0x1100               ; Argument 3: Pointer to boot video info
    push dword 0x1004               ; Argument 2: Pointer to memory map entry array
    push dword [0x1000]             ; Argument 1: Value of total entries detected
    
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

gdt_flush:
    mov eax, [esp+4]  
    lgdt [eax]        

    mov ax, 0x10      
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    jmp 0x08:.flush_done

.flush_done:
    ret

idt_flush:
    mov eax, [esp+4]
    lidt [eax]        
    ret

isr_common_stub:
    pusha             

    mov ax, ds        
    push eax          

    mov ax, 0x10      
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp          
    call isr_handler  
    add esp, 4        

    pop eax           
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa              
    add esp, 8        
    iret              

irq_common_stub:
    pusha

    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp          
    call irq_handler    
    add esp, 4        

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8
    iret

%macro ISR_NOERRCODE 1
  global isr%1
  isr%1:
    push byte 0       
    push byte %1      
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
  global isr%1
  isr%1:
    push byte %1      
    jmp isr_common_stub
%endmacro

%macro IRQ 2
  global irq%1
  irq%1:
    push byte 0        
    push byte %2        
    jmp irq_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_ERRCODE   21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

section .bss
align 16
stack_bottom:
    resb 16384        
stack_top:
