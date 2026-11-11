BITS 32

section .text
global pit_irq_stub
extern pit_handler

pit_irq_stub:
    push dword 0
    push dword 32

    pusha

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call pit_handler

    pop gs
    pop fs
    pop es
    pop ds

    popa

    add esp, 8
    iretd
