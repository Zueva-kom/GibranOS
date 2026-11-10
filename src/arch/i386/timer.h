
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(void);
uint32_t timer_get_ticks(void);
uint32_t pit_handler(uint32_t current_esp);
void pit_irq_stub(void);

#endif
