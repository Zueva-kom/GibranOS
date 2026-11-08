
#include "process.h"

static pcb_t process_table[MAX_PROCESSES];
static tcb_t thread_table[MAX_THREADS];

static uint8_t thread_stacks[MAX_THREADS][KERNEL_STACK_SIZE]
    __attribute__((aligned(16)));

static tcb_t* current_thread = NULL;

static uint32_t next_pid = 1;
static uint32_t next_tid = 1;

static void copy_name(char* dest, const char* src, size_t max_len) {
    size_t i = 0;

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }

    dest[i] = '\0';
}

static int find_free_process_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!process_table[i].used) {
            return i;
        }
    }

    return -1;
}

static int find_free_thread_slot(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (thread_table[i].tid == 0) {
            return i;
        }
    }

    return -1;
}

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid = 0;
        process_table[i].state = PROCESS_TERMINATED;
        process_table[i].used = 0;
        process_table[i].main_thread = NULL;
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        thread_table[i].tid = 0;
        thread_table[i].state = PROCESS_TERMINATED;
        thread_table[i].esp = 0;
        thread_table[i].stack_base = 0;
        thread_table[i].priority = 0;
        thread_table[i].time_slice = 0;
        thread_table[i].parent_process = NULL;
        thread_table[i].next = NULL;
    }

    current_thread = NULL;
    next_pid = 1;
    next_tid = 1;

    pcb_t* kernel_proc = process_create(
        "kernel_idle",
        NULL,
        0,
        PRIVILEGE_KERNEL
    );

    if (kernel_proc != NULL && kernel_proc->main_thread != NULL) {
        process_set_state(kernel_proc->main_thread, PROCESS_RUNNING);
        current_thread = kernel_proc->main_thread;
    }
}

pcb_t* process_create(
    const char* name,
    void (*entry_point)(void),
    uint32_t priority,
    privilege_level_t priv
) {
    int process_slot = find_free_process_slot();
    int thread_slot = find_free_thread_slot();

    if (process_slot < 0 || thread_slot < 0) {
        return NULL;
    }

    pcb_t* proc = &process_table[process_slot];
    tcb_t* thread = &thread_table[thread_slot];

    proc->pid = next_pid++;
    copy_name(proc->name, name, sizeof(proc->name));
    proc->state = PROCESS_NEW;
    proc->privilege = priv;
    proc->main_thread = thread;
    proc->used = 1;

    thread->tid = next_tid++;
    thread->state = PROCESS_NEW;
    thread->priority = priority;
    thread->time_slice = 10;
    thread->parent_process = proc;
    thread->next = NULL;

    uint32_t stack_top =
        (uint32_t)&thread_stacks[thread_slot][KERNEL_STACK_SIZE];

    thread->stack_base =
        (uint32_t)&thread_stacks[thread_slot][0];

    if (entry_point != NULL) {
        cpu_context_t* ctx =
            (cpu_context_t*)(stack_top - sizeof(cpu_context_t));

        ctx->edi = 0;
        ctx->esi = 0;
        ctx->ebx = 0;
        ctx->ebp = 0;
        ctx->eip = (uint32_t)entry_point;

        thread->esp = (uint32_t)ctx;
    } else {
        thread->esp = stack_top;
    }

    process_set_state(thread, PROCESS_READY);

    return proc;
}

void process_set_state(tcb_t* thread, process_state_t new_state) {
    if (thread == NULL) {
        return;
    }

    thread->state = new_state;

    if (thread->parent_process != NULL) {
        thread->parent_process->state = new_state;
    }
}

tcb_t* process_get_current_thread(void) {
    return current_thread;
}

const char* process_state_to_str(process_state_t state) {
    switch (state) {
        case PROCESS_NEW:
            return "NEW";

        case PROCESS_READY:
            return "READY";

        case PROCESS_RUNNING:
            return "RUNNING";

        case PROCESS_WAITING:
            return "WAITING";

        case PROCESS_TERMINATED:
            return "TERMINATED";

        default:
            return "UNKNOWN";
    }
}
