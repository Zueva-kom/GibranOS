
BITS 32

section .text
global pit_irq_stub
extern pit_handler

pit_irq_stub:
    ; Simpan nomor interrupt dan error code
    push dword 0
    push dword 32

    ; Simpan register umum
    pusha

    ; Simpan segment register dalam slot 32-bit
    xor eax, eax
    mov ax, ds
    push eax

    xor eax, eax
    mov ax, es
    push eax

    xor eax, eax
    mov ax, fs
    push eax

    xor eax, eax
    mov ax, gs
    push eax

    ; Gunakan segment kernel
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Kirim ESP saat ini ke handler C
    push esp
    call pit_handler
    add esp, 4

    ; Gunakan ESP thread yang dipilih scheduler
    mov esp, eax

    ; Pulihkan segment register
    pop eax
    mov gs, ax

    pop eax
    mov fs, ax

    pop eax
    mov es, ax

    pop eax
    mov ds, ax

    ; Pulihkan register umum
    popa

    ; Hapus interrupt number dan error code
    add esp, 8

    ; Kembali ke konteks thread
    iretd
