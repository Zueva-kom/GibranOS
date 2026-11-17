
#include "scheduler.h"

static tcb_t* thread_list = NULL;
static tcb_t* current_scheduled_thread = NULL;
static volatile uint32_t context_switch_count = 0;

void scheduler_init(void) {
    thread_list = NULL;
    current_scheduled_thread = NULL;
    context_switch_count = 0;
}

void scheduler_add_thread(tcb_t* thread) {
    if (thread == NULL) {
        return;
    }

    thread->next = NULL;

    if (thread_list == NULL) {
        thread_list = thread;
        return;
    }

    tcb_t* temp = thread_list;

    while (temp->next != NULL) {
        temp = temp->next;
    }

    temp->next = thread;
}

/* Mencari thread READY setelah thread yang sedang berjalan */
tcb_t* scheduler_pick_next(void) {
    if (thread_list == NULL) {
        return NULL;
    }

    tcb_t* start = thread_list;

    if (current_scheduled_thread != NULL) {
        start = current_scheduled_thread->next;

        if (start == NULL) {
            start = thread_list;
        }
    }

    tcb_t* candidate = start;

    do {
        if (candidate->state == PROCESS_READY) {
            return candidate;
        }

        candidate = candidate->next;

        if (candidate == NULL) {
            candidate = thread_list;
        }

    } while (candidate != start);

    return NULL;
}

/* Menjalankan algoritma Round Robin */
tcb_t* scheduler_tick(void) {
    if (current_scheduled_thread != NULL &&
        current_scheduled_thread->state == PROCESS_RUNNING) {

        current_scheduled_thread->time_slice++;
        current_scheduled_thread->total_ticks++;

        if (current_scheduled_thread->time_slice < SCHEDULER_QUANTUM) {
            return current_scheduled_thread;
        }

        current_scheduled_thread->time_slice = 0;
        process_set_state(current_scheduled_thread, PROCESS_READY);
    }

    tcb_t* next = scheduler_pick_next();

    if (next != NULL) {
        if (next != current_scheduled_thread) {
            context_switch_count++;
            next->switches++;
        }
        current_scheduled_thread = next;
        next->time_slice = 0;

        process_set_state(next, PROCESS_RUNNING);
        process_set_current_thread(next);
    }

    return current_scheduled_thread;
}

tcb_t* scheduler_get_current(void) {
    return current_scheduled_thread;
}

uint32_t scheduler_get_switch_count(void) {
    return context_switch_count;
}
