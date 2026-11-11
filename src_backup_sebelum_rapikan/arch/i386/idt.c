
#include "idt.h"

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_pointer;

void idt_set_gate(uint8_t vector, uint32_t handler) {
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].type_attr = 0x8E;
    idt[vector].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_init(void) {
    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (uint32_t)&idt;

    // Kosongkan semua entri IDT terlebih dahulu.
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }

    __asm__ __volatile__ ("lidt %0" : : "m"(idt_pointer));
}
