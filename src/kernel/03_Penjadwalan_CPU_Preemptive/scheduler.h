
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "../02_Pengelolaan_Proses_Thread/process.h"

#define SCHEDULER_QUANTUM 5

void scheduler_init(void);
void scheduler_add_thread(tcb_t* thread);
tcb_t* scheduler_pick_next(void);
void scheduler_tick(void);
tcb_t* scheduler_get_current(void);

#endif
