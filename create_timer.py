from pathlib import Path

files = {
    "src/arch/i386/timer.h": r"""
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(void);
uint32_t timer_get_ticks(void);
void pit_irq_stub(void);

#endif
""",
    "src/arch/i386/timer.c": r"""
#include "timer.h"
#include "idt.h"
#include "../../drivers/io.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

#define PIT_FREQUENCY 1193182
#define TIMER_HZ 100

static volatile uint32_t ticks = 0;

void pit_handler(void) {
    ticks++;
    outb(PIC1_COMMAND, 0x20);
}

uint32_t timer_get_ticks(void) {
    return ticks;
}

void timer_init(void) {
    /* Pasang handler IRQ0 pada interrupt vector 32 */
    idt_set_gate(32, (uint32_t)pit_irq_stub);

    /* Remap PIC master dan slave */
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Aktifkan hanya IRQ0 pada PIC master */
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);

    /* Atur PIT pada frekuensi 100 Hz */
    uint16_t divisor = PIT_FREQUENCY / TIMER_HZ;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));

    ticks = 0;

    /* Aktifkan interrupt setelah handler siap */
    __asm__ volatile ("sti");
}
""",
    "src/arch/i386/timer_irq.s": r"""
BITS 32

section .text
global pit_irq_stub
extern pit_handler

pit_irq_stub:
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
    iretd
"""
}

for name, content in files.items():
    Path(name).write_text(content.strip() + "\n", encoding="ascii")

print("File timer.h, timer.c, dan timer_irq.s berhasil dibuat.")
