#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES    16
#define MAX_THREADS      16
#define KERNEL_STACK_SIZE 4096

// 1. Transisi Status Proses & Thread sesuai spesifikasi panduan
typedef enum {
    PROCESS_NEW = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_TERMINATED
} process_state_t;

// 2. Hak Akses untuk integrasi modul Security (Anggota 4)
typedef enum {
    PRIVILEGE_KERNEL = 0,
    PRIVILEGE_USER   = 3
} privilege_level_t;

// 3. Struktur Register CPU x86 (32-bit) untuk Context Switching (Anggota 3)
typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
    uint32_t ebp;
    uint32_t eip; // Instruksi berikutnya yang akan dieksekusi
} cpu_context_t;

struct pcb; // Forward declaration

// 4. Thread Control Block (TCB) - Unit yang dijadwalkan oleh CPU
typedef struct tcb {
    uint32_t        tid;            // Thread ID
    process_state_t state;          // Status eksekusi saat ini
    uint32_t        esp;            // Stack Pointer saat ini (dipakai saat context switch)
    uint32_t        stack_base;     // Alamat dasar stack thread
    uint32_t        priority;       // Prioritas penjadwalan
    uint32_t        time_slice;     // Sisa kuota waktu (Round Robin)
    struct pcb*     parent_process; // Pointer ke PCB induknya
    struct tcb*     next;           // Pointer untuk antrean Ready Queue / Wait Queue
} tcb_t;

// 5. Process Control Block (PCB) - Wadah sumber daya & hak akses
typedef struct pcb {
    uint32_t          pid;          // Process ID
    char              name[32];     // Nama proses
    process_state_t   state;        // Status proses
    privilege_level_t privilege;    // Kernel Mode atau User Mode
    tcb_t*            main_thread;  // Pointer ke thread utama
    uint8_t           used;         // Penanda slot tabel sedang dipakai
} pcb_t;

// Fungsi dasar manajemen proses & thread
void   process_init(void);
pcb_t* process_create(const char* name, void (*entry_point)(void), uint32_t priority, privilege_level_t priv);
void   process_set_state(tcb_t* thread, process_state_t new_state);
tcb_t* process_get_current_thread(void);
const char* process_state_to_str(process_state_t state);

#endif