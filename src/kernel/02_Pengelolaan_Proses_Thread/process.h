
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

/* Batas maksimum proses dan thread */
#define MAX_PROCESSES 16
#define MAX_THREADS 16
#define KERNEL_STACK_SIZE 4096

/* Status proses dan thread */
typedef enum {
    PROCESS_NEW = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_TERMINATED
} process_state_t;

/* Tingkat hak akses proses */
typedef enum {
    PRIVILEGE_KERNEL = 0,
    PRIVILEGE_USER = 3
} privilege_level_t;

/*
 * Struktur konteks CPU untuk interrupt dan context switching.
 *
 * Urutan field harus sesuai dengan susunan stack pada
 * interrupt assembly:
 *
 * gs, fs, es, ds,
 * edi, esi, ebp, esp_dummy,
 * ebx, edx, ecx, eax,
 * interrupt_number, error_code,
 * eip, cs, eflags
 */
typedef struct {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;

    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t interrupt_number;
    uint32_t error_code;

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} cpu_context_t;

/* Deklarasi awal PCB */
struct pcb;

/* Thread Control Block (TCB) */
typedef struct tcb {
    uint32_t tid;
    process_state_t state;

    /* Alamat stack dan konteks CPU thread */
    uint32_t esp;
    uint32_t stack_base;

    /* Pengaturan penjadwalan */
    uint32_t priority;
    uint32_t time_slice;

    /* Hubungan thread dengan proses */
    struct pcb* parent_process;
    struct tcb* next;
} tcb_t;

/* Process Control Block (PCB) */
typedef struct pcb {
    uint32_t pid;
    char name[32];

    process_state_t state;
    privilege_level_t privilege;

    /* Thread utama milik proses */
    tcb_t* main_thread;

    /* Penanda slot proses sedang digunakan */
    uint8_t used;
} pcb_t;

/* Inisialisasi manajemen proses dan thread */
void process_init(void);

/* Membuat proses baru beserta thread utamanya */
pcb_t* process_create(
    const char* name,
    void (*entry_point)(void),
    uint32_t priority,
    privilege_level_t priv
);

/* Mengubah status thread */
void process_set_state(
    tcb_t* thread,
    process_state_t new_state
);

/* Mengambil thread yang sedang berjalan */
tcb_t* process_get_current_thread(void);
void process_set_current_thread(tcb_t* thread);

/* Mengubah status proses menjadi teks */
const char* process_state_to_str(process_state_t state);

/* Pengujian Process Management */
void process_test(void);


/* Mengambil data proses berdasarkan indeks */
pcb_t* process_get_by_index(uint32_t index);

/* Mengambil jumlah proses yang aktif */
uint32_t process_get_count(void);

#endif
