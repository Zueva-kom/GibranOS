
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES 16
#define MAX_THREADS 16
#define KERNEL_STACK_SIZE 4096

typedef enum {
    PROCESS_NEW = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_TERMINATED
} process_state_t;

typedef enum {
    PRIVILEGE_KERNEL = 0,
    PRIVILEGE_USER = 3
} privilege_level_t;

/*
 * Konteks register yang disimpan saat context switching.
 * Urutannya akan disesuaikan dengan interrupt assembly.
 */
typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
    uint32_t ebp;
    uint32_t eip;
} cpu_context_t;

struct pcb;

typedef struct tcb {
    uint32_t tid;
    process_state_t state;

    uint32_t esp;
    uint32_t stack_base;

    uint32_t priority;
    uint32_t time_slice;

    struct pcb* parent_process;
    struct tcb* next;
} tcb_t;

typedef struct pcb {
    uint32_t pid;
    char name[32];

    process_state_t state;
    privilege_level_t privilege;

    tcb_t* main_thread;
    uint8_t used;
} pcb_t;

void process_init(void);

pcb_t* process_create(
    const char* name,
    void (*entry_point)(void),
    uint32_t priority,
    privilege_level_t priv
);

void process_set_state(tcb_t* thread, process_state_t new_state);

tcb_t* process_get_current_thread(void);

const char* process_state_to_str(process_state_t state);

#endif
