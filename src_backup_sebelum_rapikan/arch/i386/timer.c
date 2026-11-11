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

    /* Aktifkan IRQ0 (timer) dan IRQ1 (keyboard) */
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);

    /* Atur PIT pada frekuensi 100 Hz */
    uint16_t divisor = PIT_FREQUENCY / TIMER_HZ;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));

    ticks = 0;

    /* Interrupt akan diaktifkan setelah seluruh inisialisasi siap. */
}

