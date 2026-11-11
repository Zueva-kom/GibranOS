
#include "timer.h"
#include "idt.h"
#include "../../drivers/io.h"
#include "../../kernel/03_Penjadwalan_CPU_Preemptive/scheduler.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

#define PIT_FREQUENCY 1193182
#define TIMER_HZ 100

static volatile uint32_t ticks = 0;
static uint32_t idle_esp = 0;

/* Menyimpan konteks dan memilih stack thread berikutnya */
uint32_t pit_handler(uint32_t current_esp) {
    ticks++;

    tcb_t* current = scheduler_get_current();

    if (current != NULL) {
        if (current->state == PROCESS_RUNNING) {
            current->esp = current_esp;
        }
    } else {
        /* Menyimpan konteks kernel sebelum thread pertama berjalan */
        idle_esp = current_esp;
    }

    tcb_t* next = scheduler_tick();

    /* End Of Interrupt untuk PIC master */
    outb(PIC1_COMMAND, 0x20);

    if (next != NULL && next->esp != 0) {
        return next->esp;
    }

    /* Kembali ke konteks kernel jika belum ada thread */
    if (idle_esp != 0) {
        return idle_esp;
    }

    return current_esp;
}

uint32_t timer_get_ticks(void) {
    return ticks;
}

void timer_init(void) {
    ticks = 0;
    idle_esp = 0;

    /* Pasang handler IRQ0 pada IDT vector 32 */
    idt_set_gate(32, (uint32_t)pit_irq_stub);

    /* Remap PIC */
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Aktifkan hanya IRQ0 (timer), matikan IRQ1 (keyboard) */
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);

    /* Atur PIT menjadi 100 Hz */
    uint16_t divisor = PIT_FREQUENCY / TIMER_HZ;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
}
