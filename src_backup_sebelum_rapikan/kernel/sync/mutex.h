#ifndef MUTEX_H
#define MUTEX_H

#include <stdint.h>
#include "process/process.h"

typedef struct {
    volatile uint8_t locked;      // Status kunci: 0 = Terbuka, 1 = Terkunci
    tcb_t*           owner;       // Thread yang sedang memegang kunci
    tcb_t*           wait_queue;  // Antrean thread yang menunggu (WAITING)
} mutex_t;

void mutex_init(mutex_t* m);
void mutex_lock(mutex_t* m);
void mutex_unlock(mutex_t* m);

#endif