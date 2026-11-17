#include "deadlock.h"
#include "../02_Pengelolaan_Proses_Thread/process.h"
#include "../04_Sinkronisasi/mutex.h"
#include "../../arch/i386/timer.h"
#include <stdint.h>
#include <stddef.h>

/* Fungsi output VGA dari kernel/main.c */
extern void vga_print(const char* str, uint8_t color);
extern void vga_putchar(char c, uint8_t color);

/* ------------------------------------------------------------------------
 * Konfigurasi demo
 * ------------------------------------------------------------------------ */
#define DL_HOLD_TICKS     60   /* Lama thread menahan kunci pertama (0.6 s)  */
#define DL_CONFIRM_TICKS  100  /* Deadlock harus bertahan 1 s agar dikonfirmasi */
#define DL_TIMEOUT_TICKS  600  /* Batas waktu pemantauan oleh Shell (6 s)    */
#define DL_MAX_EVENTS     24

/* Dua sumber daya bersama (R1 dan R2) */
static mutex_t dl_mutex_score;   /* R1: melindungi skor permainan         */
static mutex_t dl_mutex_vfs;     /* R2: melindungi berkas scores.txt (VFS) */

static volatile int      dl_lock_ordering = 0;
static volatile uint32_t dl_done_count    = 0;

/* ------------------------------------------------------------------------
 * Log kejadian: thread demo TIDAK mencetak langsung ke VGA (vga_print tidak
 * thread-safe). Mereka mencatat ke log, lalu hanya thread Shell yang mencetak.
 * ------------------------------------------------------------------------ */
typedef struct {
    uint32_t    tick;
    const char* who;
    const char* msg;
    uint8_t     color;
} dl_event_t;

static dl_event_t        dl_events[DL_MAX_EVENTS];
static volatile uint32_t dl_event_count = 0;

static inline uint32_t dl_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void dl_irq_restore(uint32_t flags) {
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static void dl_log(const char* who, const char* msg, uint8_t color) {
    uint32_t f = dl_irq_save();
    if (dl_event_count < DL_MAX_EVENTS) {
        dl_event_t* e = &dl_events[dl_event_count];
        e->tick  = timer_get_ticks();
        e->who   = who;
        e->msg   = msg;
        e->color = color;
        dl_event_count++;
    }
    dl_irq_restore(f);
}

static void dl_print_uint(uint32_t n, uint8_t color) {
    char buf[12];
    int i = 0;
    if (n == 0) {
        vga_putchar('0', color);
        return;
    }
    while (n > 0 && i < 11) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (i > 0) {
        vga_putchar(buf[--i], color);
    }
}

static void dl_print_padded(const char* s, int width, uint8_t color) {
    int n = 0;
    while (s[n] != '\0') {
        vga_putchar(s[n], color);
        n++;
    }
    while (n < width) {
        vga_putchar(' ', 0x07);
        n++;
    }
}

/* Mencetak log baru yang belum tampil (dipanggil hanya oleh Shell) */
static uint32_t dl_flush_events(uint32_t printed, uint32_t start_tick) {
    while (printed < dl_event_count) {
        dl_event_t* e = &dl_events[printed];
        vga_print("  [t+", 0x08);
        dl_print_uint(e->tick - start_tick, 0x08);
        vga_print("] ", 0x08);
        dl_print_padded(e->who, 10, 0x0B);
        vga_print(": ", 0x07);
        vga_print(e->msg, e->color);
        vga_putchar('\n', 0x07);
        printed++;
    }
    return printed;
}

/* Menahan kunci beberapa saat agar thread lain sempat mengambil kunci miliknya */
static void dl_hold(uint32_t ticks) {
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < ticks) {
        __asm__ volatile ("hlt");
    }
}

/* Thread selesai: tandai TERMINATED, scheduler tidak akan memilihnya lagi */
static void dl_finish(void) {
    __atomic_fetch_add(&dl_done_count, 1, __ATOMIC_SEQ_CST);
    tcb_t* self = process_get_current_thread();
    if (self != NULL) {
        process_set_state(self, PROCESS_TERMINATED);
    }
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/* ------------------------------------------------------------------------
 * THREAD 1: GameInput -> selalu: mutex_score (R1) lalu mutex_vfs (R2)
 * ------------------------------------------------------------------------ */
static void dl_thread_game_input(void) {
    dl_log("GameInput", "lock(mutex_score) ...", 0x07);
    mutex_lock(&dl_mutex_score);
    dl_log("GameInput", "MEMEGANG mutex_score (R1)", 0x0A);

    dl_hold(DL_HOLD_TICKS);

    dl_log("GameInput", "lock(mutex_vfs) ... menunggu R2", 0x0E);
    mutex_lock(&dl_mutex_vfs);
    dl_log("GameInput", "MEMEGANG R1+R2 -> update skor & simpan", 0x0A);

    mutex_unlock(&dl_mutex_vfs);
    mutex_unlock(&dl_mutex_score);
    dl_log("GameInput", "unlock R2, R1 -> selesai", 0x0A);

    dl_finish();
}

/* ------------------------------------------------------------------------
 * THREAD 2: AutoSave
 *   mode deadlock     : mutex_vfs (R2) lalu mutex_score (R1)  <- BERLAWANAN
 *   mode deadlock_fix : mutex_score (R1) lalu mutex_vfs (R2)  <- urutan global
 * ------------------------------------------------------------------------ */
static void dl_thread_auto_save(void) {
    mutex_t*    first       = dl_lock_ordering ? &dl_mutex_score : &dl_mutex_vfs;
    mutex_t*    second      = dl_lock_ordering ? &dl_mutex_vfs   : &dl_mutex_score;
    const char* try_first   = dl_lock_ordering ? "lock(mutex_score) ..." : "lock(mutex_vfs) ...";
    const char* got_first   = dl_lock_ordering ? "MEMEGANG mutex_score (R1)" : "MEMEGANG mutex_vfs (R2)";
    const char* try_second  = dl_lock_ordering ? "lock(mutex_vfs) ... menunggu R2"
                                               : "lock(mutex_score) ... menunggu R1";

    dl_log("AutoSave", try_first, 0x07);
    mutex_lock(first);
    dl_log("AutoSave", got_first, 0x0A);

    dl_hold(DL_HOLD_TICKS);

    dl_log("AutoSave", try_second, 0x0E);
    mutex_lock(second);
    dl_log("AutoSave", "MEMEGANG R1+R2 -> tulis scores.txt", 0x0A);

    mutex_unlock(second);
    mutex_unlock(first);
    dl_log("AutoSave", "unlock kedua mutex -> selesai", 0x0A);

    dl_finish();
}

/* ------------------------------------------------------------------------
 * Deteksi circular wait (wait-for graph 2 node):
 *   GameInput menunggu R2, dan R2 dipegang AutoSave
 *   AutoSave  menunggu R1, dan R1 dipegang GameInput
 * ------------------------------------------------------------------------ */
static int dl_is_circular_wait(tcb_t* ta, tcb_t* tb) {
    return ta->state == PROCESS_WAITING &&
           tb->state == PROCESS_WAITING &&
           ta->waiting_on == (void*)&dl_mutex_vfs   && dl_mutex_vfs.owner   == tb &&
           tb->waiting_on == (void*)&dl_mutex_score && dl_mutex_score.owner == ta;
}

static const char* dl_mutex_name(void* m) {
    if (m == (void*)&dl_mutex_score) return "mutex_score";
    if (m == (void*)&dl_mutex_vfs)   return "mutex_vfs";
    return "-";
}

static void dl_print_thread_row(const char* name, tcb_t* t) {
    const char* holds;
    if (dl_mutex_score.owner == t && dl_mutex_vfs.owner == t) holds = "R1+R2";
    else if (dl_mutex_score.owner == t) holds = "mutex_score";
    else if (dl_mutex_vfs.owner == t)   holds = "mutex_vfs";
    else holds = "-";

    vga_print("  ", 0x07);
    dl_print_padded(name, 11, 0x0F);
    dl_print_uint(t->tid, 0x07);
    vga_print(t->tid < 10 ? "    " : "   ", 0x07);
    dl_print_padded(process_state_to_str(t->state), 12,
                    t->state == PROCESS_WAITING ? 0x0C :
                    (t->state == PROCESS_TERMINATED ? 0x08 : 0x0A));
    dl_print_padded(holds, 13, 0x0E);
    vga_print(dl_mutex_name(t->waiting_on), 0x0C);
    vga_putchar('\n', 0x07);
}

static void dl_print_table(tcb_t* ta, tcb_t* tb) {
    vga_print("  THREAD     TID  STATE       MEMEGANG     MENUNGGU\n", 0x0B);
    dl_print_thread_row("GameInput", ta);
    dl_print_thread_row("AutoSave", tb);
}

/* ------------------------------------------------------------------------
 * Dipanggil dari CLI (thread Shell)
 * ------------------------------------------------------------------------ */
void deadlock_demo_run(int use_lock_ordering) {
    mutex_init(&dl_mutex_score);
    mutex_init(&dl_mutex_vfs);
    dl_lock_ordering = use_lock_ordering;
    dl_done_count    = 0;
    dl_event_count   = 0;

    if (!use_lock_ordering) {
        vga_print("=== [5] DEMONSTRASI DEADLOCK (CIRCULAR WAIT) ===\n", 0x0C);
        vga_print("GameInput: lock R1(mutex_score) -> lock R2(mutex_vfs)\n", 0x07);
        vga_print("AutoSave : lock R2(mutex_vfs)   -> lock R1(mutex_score)  <- urutan berlawanan\n\n", 0x07);
    } else {
        vga_print("=== [5] SOLUSI DEADLOCK (LOCK ORDERING) ===\n", 0x0A);
        vga_print("Aturan global: SELALU kunci R1(mutex_score) dulu, baru R2(mutex_vfs).\n", 0x07);
        vga_print("GameInput & AutoSave: lock R1 -> lock R2\n\n", 0x07);
    }

    /* Catat waktu mulai SEBELUM thread dibuat (thread bisa jalan saat Shell mencetak) */
    uint32_t start = timer_get_ticks();

    pcb_t* pa = process_create("GameInput", dl_thread_game_input, 1, PRIVILEGE_KERNEL);
    pcb_t* pb = process_create("AutoSave",  dl_thread_auto_save,  1, PRIVILEGE_KERNEL);

    if (pa == NULL || pb == NULL || pa->main_thread == NULL || pb->main_thread == NULL) {
        vga_print("[ERROR] Gagal membuat thread demo (tabel proses penuh).\n", 0x0C);
        process_terminate(pa);
        process_terminate(pb);
        return;
    }

    tcb_t* ta = pa->main_thread;
    tcb_t* tb = pb->main_thread;

    vga_print("[KERNEL] Thread GameInput (TID ", 0x0B);
    dl_print_uint(ta->tid, 0x0B);
    vga_print(") & AutoSave (TID ", 0x0B);
    dl_print_uint(tb->tid, 0x0B);
    vga_print(") dibuat -> READY, dijadwalkan Round Robin.\n", 0x0B);

    uint32_t printed       = 0;
    uint32_t deadlock_tick = 0;
    int      deadlocked    = 0;

    /* Shell memantau; selama loop ini thread demo berjalan lewat preemption */
    while (timer_get_ticks() - start < DL_TIMEOUT_TICKS) {
        printed = dl_flush_events(printed, start);

        if (dl_done_count >= 2) {
            break;
        }

        if (dl_is_circular_wait(ta, tb)) {
            if (deadlock_tick == 0) {
                deadlock_tick = timer_get_ticks();
            } else if (timer_get_ticks() - deadlock_tick >= DL_CONFIRM_TICKS) {
                deadlocked = 1;
                break;
            }
        } else {
            deadlock_tick = 0;
        }

        __asm__ volatile ("pause");
    }
    printed = dl_flush_events(printed, start);
    vga_putchar('\n', 0x07);

    if (deadlocked) {
        vga_print("[DEADLOCK!] Circular wait terdeteksi (bertahan > 1 detik):\n", 0x0C);
        vga_print("  GameInput memegang R1, menunggu R2  --->  R2 dipegang AutoSave\n", 0x0C);
        vga_print("  AutoSave  memegang R2, menunggu R1  --->  R1 dipegang GameInput\n", 0x0C);
        vga_print("  Coffman: mutual exclusion, hold & wait, no preemption, circular wait.\n\n", 0x07);
        dl_print_table(ta, tb);

        /* Recovery: kernel menghentikan paksa kedua thread & membebaskan kunci */
        process_terminate(pa);
        process_terminate(pb);
        mutex_init(&dl_mutex_score);
        mutex_init(&dl_mutex_vfs);

        vga_print("\n[RECOVERY] Kernel menghentikan GameInput & AutoSave -> TERMINATED,\n", 0x0E);
        vga_print("           mutex_score & mutex_vfs dilepas paksa. Shell tetap berjalan.\n", 0x0E);
        vga_print("Jalankan 'deadlock_fix' untuk melihat pencegahan dengan Lock Ordering.\n", 0x07);

    } else if (dl_done_count >= 2) {
        dl_print_table(ta, tb);
        vga_print("\n[AMAN] Kedua thread selesai -> TERMINATED. Tidak terjadi circular wait,\n", 0x0A);
        vga_print("       karena thread kedua tertahan di R1 (WAITING) sampai thread pertama\n", 0x0A);
        vga_print("       melepas R1+R2, lalu baru melanjutkan.\n", 0x0A);
        if (!use_lock_ordering) {
            vga_print("(Kali ini deadlock tidak terjadi karena urutan eksekusi; ulangi perintah.)\n", 0x0E);
        }

    } else {
        vga_print("[TIMEOUT] Demo tidak selesai dalam 6 detik.\n", 0x0E);
        dl_print_table(ta, tb);
        process_terminate(pa);
        process_terminate(pb);
        mutex_init(&dl_mutex_score);
        mutex_init(&dl_mutex_vfs);
    }

    vga_print("Cek 'ps' untuk melihat status GameInput & AutoSave.\n", 0x08);
}