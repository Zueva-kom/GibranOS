#include "mutex.h"

static inline uint32_t mutex_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void mutex_irq_restore(uint32_t flags) {
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

void mutex_init(mutex_t* m) {
    if (!m) return;
    m->locked = 0;
    m->owner = NULL;
    m->wait_queue = NULL;
}

void mutex_lock(mutex_t* m) {
    if (!m) return;
    tcb_t* current = process_get_current_thread();

    while (1) {
        /*
         * Cek kunci + ubah status dilakukan dengan interrupt mati, agar timer
         * tidak memotong di tengah-tengah (mencegah "lost wake-up").
         */
        uint32_t flags = mutex_irq_save();

        if (!__atomic_test_and_set(&m->locked, __ATOMIC_ACQUIRE)) {
            m->owner = current;
            if (current) {
                current->waiting_on = NULL;
                process_set_state(current, PROCESS_RUNNING);
            }
            mutex_irq_restore(flags);
            return;
        }

        /* Kunci sedang dipegang thread lain -> thread ini masuk WAITING */
        if (current) {
            current->waiting_on = (void*)m;
            process_set_state(current, PROCESS_WAITING);
        }
        mutex_irq_restore(flags);

        /*
         * Tunggu timer interrupt berikutnya. Karena status WAITING, scheduler
         * Round Robin melewati thread ini sampai mutex_unlock() membuatnya READY.
         */
        __asm__ volatile ("hlt");
    }
}

void mutex_unlock(mutex_t* m) {
    if (!m) return;

    uint32_t flags = mutex_irq_save();

    m->owner = NULL;
    __atomic_clear(&m->locked, __ATOMIC_RELEASE);

    /* Bangunkan thread yang sedang menunggu mutex ini agar kembali READY */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        pcb_t* proc = process_get_by_index(i);
        if (proc != NULL && proc->main_thread != NULL) {
            tcb_t* t = proc->main_thread;
            if (t->state == PROCESS_WAITING && t->waiting_on == (void*)m) {
                t->waiting_on = NULL;
                process_set_state(t, PROCESS_READY);
            }
        }
    }

    mutex_irq_restore(flags);
}
