#include "mutex.h"

void mutex_init(mutex_t* m) {
    if (!m) return;
    m->locked = 0;
    m->owner = NULL;
    m->wait_queue = NULL;
}

void mutex_lock(mutex_t* m) {
    if (!m) return;
    tcb_t* current = process_get_current_thread();

    // Operasi atomik: coba ambil kunci. Jika sudah bernilai 1, tunggu.
    while (__atomic_test_and_set(&m->locked, __ATOMIC_ACQUIRE)) {
        if (current) {
            process_set_state(current, PROCESS_WAITING);
        }
        // Istirahatkan CPU sejenak sambil menunggu giliran scheduler
        __asm__ volatile ("pause");
    }

    if (current) {
        process_set_state(current, PROCESS_RUNNING);
        m->owner = current;
    }
}

void mutex_unlock(mutex_t* m) {
    if (!m) return;
    m->owner = NULL;
    __atomic_clear(&m->locked, __ATOMIC_RELEASE);
}