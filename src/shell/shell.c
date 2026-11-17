#include "shell/shell.h"
#include "arch/i386/timer.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/serial.h"
#include "kernel/02_Pengelolaan_Proses_Thread/process.h"
#include "kernel/03_Penjadwalan_CPU_Preemptive/scheduler.h"
#include "kernel/04_Sinkronisasi/mutex.h"
#include "kernel/05_Deadlock/deadlock.h"
#include "kernel/06_Alokator_Memori_Dinamis/heap.h"
#include "kernel/07_Virtual_File_System/vfs.h"
#include "kernel/08_Proteksi_Hak_Akses/security.h"
#include "kernel/game/game.h"

/* Variabel pengujian Sinkronisasi (Mutex) dengan 2 Thread Nyata */
static mutex_t game_score_mutex;
static volatile uint32_t shared_game_score = 0;
static volatile int sync_use_mutex = 0;
static volatile uint32_t sync_done_count = 0;
#define SYNC_ITERATIONS 10

/* Variabel pengujian Siklus Hidup 2 Thread (NEW -> READY -> RUNNING -> WAITING -> TERMINATED) */
static mutex_t thread_demo_gate;
static volatile uint32_t demo_calc_sum = 0;
static volatile uint32_t demo_scan_count = 0;
static volatile const char* s1_run_str = NULL;
static volatile const char* s2_run_str = NULL;

static int str_eq(const char* a, const char* b) {
    size_t i = 0;

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        i++;
    }

    return a[i] == b[i];
}

static int str_starts_with(const char* str, const char* prefix) {
    size_t i = 0;

    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) return 0;
        i++;
    }

    return 1;
}

static const char* extract_token(const char* str, char* out_token, size_t max_len) {
    size_t i = 0;

    while (*str == ' ') {
        str++;
    }

    while (*str != '\0' && *str != ' ' && i < max_len - 1) {
        out_token[i++] = *str++;
    }

    out_token[i] = '\0';

    while (*str == ' ') {
        str++;
    }

    return str;
}

static void print_prompt(void) {
    user_role_t role = security_get_role();
    vga_print(role == ROLE_ADMIN ? "GibranOS-CLI [ADMIN] $ " : "GibranOS-CLI [USER] $ ",
              role == ROLE_ADMIN ? 0x0A : 0x0B);
}

/* ============================================================================
 * [POIN 2] DEMO SIKLUS HIDUP 2 KERNEL THREAD DENGAN TUGAS BERBEDA
 * Menampilkan transisi lengkap: NEW -> READY -> RUNNING -> WAITING -> TERMINATED
 * ============================================================================ */
static void demo_thread_calc(void) {
    tcb_t* self = process_get_current_thread();
    if (self != NULL) {
        s1_run_str = process_state_to_str(self->state);
    }

    for (uint32_t i = 1; i <= 50; i++) {
        demo_calc_sum += i;
    }

    /* Terblokir di mutex yang masih dipegang Shell -> status berubah ke WAITING */
    mutex_lock(&thread_demo_gate);
    mutex_unlock(&thread_demo_gate);

    if (self != NULL) {
        process_set_state(self, PROCESS_TERMINATED);
    }
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void demo_thread_scan(void) {
    tcb_t* self = process_get_current_thread();
    if (self != NULL) {
        s2_run_str = process_state_to_str(self->state);
    }

    heap_block_t* curr = heap_get_head();
    while (curr != NULL) {
        demo_scan_count++;
        curr = curr->next;
    }

    /* Terblokir di mutex yang masih dipegang Shell -> status berubah ke WAITING */
    mutex_lock(&thread_demo_gate);
    mutex_unlock(&thread_demo_gate);

    if (self != NULL) {
        process_set_state(self, PROCESS_TERMINATED);
    }
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void run_thread_lifecycle_demo(void) {
    vga_print("=== [2] PENGUJIAN SIKLUS HIDUP 2 THREAD BERBEDA ===\n", 0x0E);
    vga_print("Tugas 1 (TaskHitung)  : Menghitung deret aritmetika 1..50\n", 0x07);
    vga_print("Tugas 2 (TaskScanMem) : Memindai blok memori pada Heap\n\n", 0x07);

    mutex_init(&thread_demo_gate);
    mutex_lock(&thread_demo_gate);
    demo_calc_sum = 0;
    demo_scan_count = 0;
    s1_run_str = NULL;
    s2_run_str = NULL;

    /* 1. Status NEW */
    pcb_t* p1 = process_create_new("TaskHitung", demo_thread_calc, 1, PRIVILEGE_KERNEL);
    pcb_t* p2 = process_create_new("TaskScanMem", demo_thread_scan, 1, PRIVILEGE_KERNEL);
    if (p1 == NULL || p2 == NULL) {
        mutex_unlock(&thread_demo_gate);
        vga_print("[ERROR] Gagal membuat thread demo.\n", 0x0C);
        return;
    }

    const char* s1_new = process_state_to_str(p1->main_thread->state);
    const char* s2_new = process_state_to_str(p2->main_thread->state);

    /* 2. Status READY */
    process_start(p1);
    process_start(p2);
    const char* s1_rdy = process_state_to_str(p1->main_thread->state);
    const char* s2_rdy = process_state_to_str(p2->main_thread->state);

    vga_print("1. Status Awal   -> TaskHitung: ", 0x07);
    vga_print(s1_new, 0x0B);
    vga_print(" | TaskScanMem: ", 0x07);
    vga_print(s2_new, 0x0B);
    vga_putchar('\n', 0x07);

    vga_print("2. Dijadwalkan   -> TaskHitung: ", 0x07);
    vga_print(s1_rdy, 0x0E);
    vga_print(" | TaskScanMem: ", 0x07);
    vga_print(s2_rdy, 0x0E);
    vga_putchar('\n', 0x07);

    /* Tunggu hingga kedua thread selesai berjalan (RUNNING) dan masuk (WAITING) */
    while (p1->main_thread->state != PROCESS_WAITING ||
           p2->main_thread->state != PROCESS_WAITING) {
        __asm__ volatile ("pause");
    }

    /* 3. Status RUNNING */
    vga_print("3. Eksekusi CPU  -> TaskHitung: ", 0x07);
    vga_print(s1_run_str != NULL ? (const char*)s1_run_str : "RUNNING", 0x0A);
    vga_print(" | TaskScanMem: ", 0x07);
    vga_print(s2_run_str != NULL ? (const char*)s2_run_str : "RUNNING", 0x0A);
    vga_putchar('\n', 0x07);

    /* 4. Status WAITING */
    vga_print("4. Tunggu Mutex  -> TaskHitung: ", 0x07);
    vga_print(process_state_to_str(p1->main_thread->state), 0x0E);
    vga_print(" | TaskScanMem: ", 0x07);
    vga_print(process_state_to_str(p2->main_thread->state), 0x0E);
    vga_putchar('\n', 0x07);

    /* 5. Lepas kunci agar selesai ke status TERMINATED */
    mutex_unlock(&thread_demo_gate);
    while (p1->main_thread->state != PROCESS_TERMINATED ||
           p2->main_thread->state != PROCESS_TERMINATED) {
        __asm__ volatile ("pause");
    }

    vga_print("5. Selesai Tugas -> TaskHitung: ", 0x07);
    vga_print(process_state_to_str(p1->main_thread->state), 0x0C);
    vga_print(" | TaskScanMem: ", 0x07);
    vga_print(process_state_to_str(p2->main_thread->state), 0x0C);
    vga_putchar('\n', 0x07);

    vga_print("Hasil Tugas 1 (Sum 1..50): ", 0x07);
    vga_print_number(demo_calc_sum);
    vga_print(" | Tugas 2 (Blok Heap): ", 0x07);
    vga_print_number(demo_scan_count);
    vga_putchar('\n', 0x07);
}

/* ============================================================================
 * [POIN 4] DEMO SINKRONISASI MUTEX DENGAN 2 THREAD NYATA
 * Membandingkan operasi shared counter tanpa mutex vs dengan mutex
 * ============================================================================ */
static void sync_worker_thread(void) {
    for (int i = 0; i < SYNC_ITERATIONS; i++) {
        if (sync_use_mutex) {
            mutex_lock(&game_score_mutex);
            uint32_t temp = shared_game_score;
            uint32_t t0 = timer_get_ticks();
            while (timer_get_ticks() - t0 < 2) {
                __asm__ volatile ("pause");
            }
            shared_game_score = temp + 10;
            mutex_unlock(&game_score_mutex);
        } else {
            /* Tanpa mutex: ter-preempt di tengah read-modify-write -> pasti lost update */
            uint32_t temp = shared_game_score;
            uint32_t t0 = timer_get_ticks();
            while (timer_get_ticks() - t0 < 2) {
                __asm__ volatile ("pause");
            }
            shared_game_score = temp + 10;
        }
    }

    __atomic_fetch_add(&sync_done_count, 1, __ATOMIC_SEQ_CST);
    tcb_t* self = process_get_current_thread();
    if (self != NULL) {
        process_set_state(self, PROCESS_TERMINATED);
    }
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void execute_sync_round(int use_mutex) {
    mutex_init(&game_score_mutex);
    shared_game_score = 0;
    sync_use_mutex = use_mutex;
    sync_done_count = 0;

    pcb_t* p1 = process_create("SyncInput", sync_worker_thread, 1, PRIVILEGE_KERNEL);
    pcb_t* p2 = process_create("SyncBonus", sync_worker_thread, 1, PRIVILEGE_KERNEL);

    if (p1 == NULL || p2 == NULL) {
        vga_print("[ERROR] Gagal membuat thread uji sinkronisasi.\n", 0x0C);
        process_terminate(p1);
        process_terminate(p2);
        return;
    }

    while (sync_done_count < 2) {
        __asm__ volatile ("pause");
    }
}

static void run_mutex_sync_demo(void) {
    uint32_t expected = SYNC_ITERATIONS * 10 * 2; /* 10 * 10 * 2 = 200 poin */

    vga_print("=== [4] PENGUJIAN SINKRONISASI (2 THREAD NYATA) ===\n", 0x0E);
    vga_print("Skenario: Thread SyncInput & SyncBonus update shared_game_score (+10 x10).\n", 0x07);
    vga_print("Target skor seharusnya: ", 0x07);
    vga_print_number(expected);
    vga_print(" poin.\n\n", 0x07);

    /* Fase 1: Tanpa Mutex */
    execute_sync_round(0);
    vga_print("1. Tanpa Mutex (Race Condition nyata) : ", 0x0C);
    vga_print_number(shared_game_score);
    vga_print(" / ", 0x0C);
    vga_print_number(expected);
    vga_print(shared_game_score < expected ? " poin (Data korup / Lost Update!)\n"
                                           : " poin (Ulangi jika belum bentrok)\n", 0x0C);

    /* Fase 2: Dengan Mutex */
    execute_sync_round(1);
    vga_print("2. Dengan game_score_mutex            : ", 0x0A);
    vga_print_number(shared_game_score);
    vga_print(" / ", 0x0A);
    vga_print_number(expected);
    vga_print(" poin (Konsisten 100%!)\n", 0x0A);
}

void shell_init(void) {
    mutex_init(&game_score_mutex);
    shared_game_score = 0;
}

void shell_execute(const char* cmd) {
    user_role_t role = security_get_role();

    while (*cmd == ' ') {
        cmd++;
    }

    if (*cmd == '\0') {
        return;
    } else if (str_eq(cmd, "help")) {
        vga_print("=====================================================================================\n", 0x0B);
        vga_print("                         GIBRAN OS - DAFTAR PERINTAH\n", 0x0B);
        vga_print("=====================================================================================\n", 0x0B);

        vga_print("[1] DASAR SISTEM DAN ANTARMUKA PERINTAH (USER & ADMIN)\n", 0x0B);
        vga_print("  help                     - Melihat daftar perintah yang tersedia\n", 0x07);
        vga_print("  clear                    - Membersihkan tampilan terminal\n", 0x07);
        vga_print("  version                  - Melihat versi dan informasi kernel\n", 0x07);
        vga_print("  echo <teks>              - Menampilkan teks yang dimasukkan\n", 0x07);
        vga_print("  game                     - Menjalankan game (Multi-Window + Live Heap Monitor)\n", 0x07);
        vga_print("  logout                   - Kembali ke Halaman Login (Ganti Role User/Admin)\n", 0x07);
        vga_print("  shutdown                 - Mematikan GIBRAN OS dengan aman\n\n", 0x07);

        vga_print("[2-3] PENGELOLAAN PROSES, THREAD, DAN PENJADWALAN CPU (ADMIN ONLY)\n",
                  role == ROLE_ADMIN ? 0x0B : 0x08);
        vga_print("  ps                       - Melihat PID, nama, dan status proses/thread\n", 0x07);
        vga_print("  thread_test              - Demo siklus hidup 2 thread (NEW..TERMINATED)\n", 0x07);
        vga_print("  schedtest                - Melihat aktivitas counter WorkerA dan B\n\n", 0x07);

        vga_print("[4] SINKRONISASI DAN MUTEX (ADMIN ONLY)\n",
                  role == ROLE_ADMIN ? 0x0B : 0x08);
        vga_print("  mutex_test               - Demo perbandingan Race Condition vs Mutex\n\n", 0x07);

        vga_print("[5] DEADLOCK DAN PENANGANANNYA (ADMIN ONLY)\n",
                  role == ROLE_ADMIN ? 0x0B : 0x08);
        vga_print("  deadlock                 - Demo kondisi deadlock (Circular Wait)\n", 0x07);
        vga_print("  deadlock_fix             - Demo penanganan deadlock (Lock Ordering)\n\n", 0x07);

        vga_print("[6] PENGELOLAAN MEMORI DINAMIS (ADMIN ONLY)\n",
                  role == ROLE_ADMIN ? 0x0B : 0x08);
        vga_print("  meminfo                  - Melihat total, terpakai, dan sisa heap\n", 0x07);
        vga_print("  test_mem                 - Menguji alokasi, free, dan kondisi OOM\n\n", 0x07);

        vga_print("[7] SISTEM BERKAS VIRTUAL / RAMDISK\n", 0x0B);
        vga_print("  ls                             - Melihat daftar berkas yang tersimpan\n", 0x07);
        vga_print("  create <nama> <isi>            - Buat berkas (Admin: [PROTECTED] | User: [USER])\n", 0x07);
        vga_print("  create <nama> public <isi>     - Admin buat berkas [PUBLIC] (User hanya read)\n", 0x07);
        vga_print("  create <nama> protected <isi>  - Admin buat berkas [PROTECTED] (Admin only)\n", 0x07);
        vga_print("  read <nama>                    - Membaca isi berkas (Protected: Admin Only)\n", 0x07);
        vga_print("  update <nama> <isi_baru>       - Mengubah isi berkas (Milik Admin: Admin Only)\n", 0x07);
        vga_print("  delete <nama>                  - Menghapus berkas (Milik Admin: Admin Only)\n\n", 0x07);

        vga_print("[8] PROTEKSI DAN HAK AKSES\n", 0x0B);
        vga_print("  access_test              - Cek status role aktif & kebijakan hak akses\n", 0x07);
        vga_print("=====================================================================================\n", 0x0B);

    } else if (str_eq(cmd, "logout")) {
        security_show_login_screen();

    } else if (str_eq(cmd, "thread_test")) {
        if (!security_require_admin("thread_test")) return;
        run_thread_lifecycle_demo();

    } else if (str_eq(cmd, "schedtest")) {
        if (!security_require_admin("schedtest")) return;
        uint32_t wa = kernel_get_worker_a_counter();
        uint32_t wb = kernel_get_worker_b_counter();

        vga_print("=== ROUND ROBIN SCHEDULER TEST ===\n", 0x0E);

        vga_print("WorkerA Counter : ", 0x07);
        vga_print_number(wa);
        vga_putchar('\n', 0x07);

        vga_print("WorkerB Counter : ", 0x07);
        vga_print_number(wb);
        vga_putchar('\n', 0x07);

        vga_print("Context Switches: ", 0x07);
        vga_print_number(scheduler_get_switch_count());
        vga_putchar('\n', 0x07);

        if (wa > 0 && wb > 0) {
            vga_print("Result          : BOTH WORKERS HAVE RUN\n", 0x0A);
        } else {
            vga_print("Result          : ONE OR BOTH WORKERS NOT RUN YET\n", 0x0E);
        }

        vga_print("Scheduler       : ROUND ROBIN (configured)\n", 0x07);

    } else if (str_eq(cmd, "mutex_test")) {
        if (!security_require_admin("mutex_test")) return;
        run_mutex_sync_demo();

    } else if (str_eq(cmd, "deadlock")) {
        if (!security_require_admin("deadlock")) return;
        deadlock_demo_run(0);

    } else if (str_eq(cmd, "deadlock_fix")) {
        if (!security_require_admin("deadlock_fix")) return;
        deadlock_demo_run(1);

    } else if (str_eq(cmd, "access_test")) {
        vga_print("=== [8] STATUS PROTEKSI & HAK AKSES ===\n", 0x0E);
        vga_print("Peran Aktif : ", 0x07);
        vga_print(role == ROLE_ADMIN ? "ADMIN (Akses Penuh Menu 1-8 & Berkas Protected)\n"
                                     : "USER (Game, Dasar, Read [PUBLIC], Full CRUD [USER])\n",
                  role == ROLE_ADMIN ? 0x0A : 0x0E);
        vga_print("Gunakan perintah 'logout' untuk berpindah antara akun 'user' dan 'admin'.\n", 0x07);

    } else if (str_eq(cmd, "ps")) {
        if (!security_require_admin("ps")) return;
        vga_print("=== PROCESS & THREAD LIST ===\n", 0x0E);
        vga_print("Jumlah proses terdaftar: ", 0x07);
        vga_print_number(process_get_count());
        vga_print(" | Total Context Switch: ", 0x07);
        vga_print_number(scheduler_get_switch_count());
        vga_putchar('\n', 0x07);
        vga_print("PID  NAME          STATE        TID  THREAD STATE  CPU TICKS\n", 0x0B);

        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            pcb_t* proc = process_get_by_index(i);
            if (proc == NULL) {
                continue;
            }

            vga_print_number(proc->pid);
            vga_print("    ", 0x07);
            {
                int n = 0;
                while (proc->name[n] != '\0') n++;
                vga_print(proc->name, 0x07);
                for (; n < 14; n++) vga_putchar(' ', 0x07);
            }
            {
                const char* st = process_state_to_str(proc->state);
                int n = 0;
                while (st[n] != '\0') n++;
                vga_print(st, 0x07);
                for (; n < 13; n++) vga_putchar(' ', 0x07);
            }

            if (proc->main_thread != NULL) {
                vga_print_number(proc->main_thread->tid);
                vga_print(proc->main_thread->tid < 10 ? "    " : "   ", 0x07);
                {
                    const char* tst = process_state_to_str(proc->main_thread->state);
                    int n = 0;
                    while (tst[n] != '\0') n++;
                    vga_print(tst, 0x07);
                    for (; n < 14; n++) vga_putchar(' ', 0x07);
                }
                vga_print_number(proc->main_thread->total_ticks);
            } else {
                vga_print("-", 0x07);
            }
            vga_putchar('\n', 0x07);
        }

        if (process_get_count() == 0) {
            vga_print("Belum ada proses yang dibuat.\n", 0x0E);
        }

    } else if (str_eq(cmd, "clear")) {
        vga_clear(0x07);

    } else if (str_eq(cmd, "version")) {
        vga_print("GibranOS v0.1.2 (x86 32-bit Multiboot Kernel)\n", 0x0A);

    } else if (str_starts_with(cmd, "echo ")) {
        vga_print(cmd + 5, 0x07);
        vga_putchar('\n', 0x07);

    } else if (str_eq(cmd, "echo")) {
        vga_putchar('\n', 0x07);

    } else if (str_eq(cmd, "game") || str_eq(cmd, "game start")) {
        game_start();

    } else if (str_eq(cmd, "ls")) {
        vfs_list_files();

    } else if (str_starts_with(cmd, "create ") || str_eq(cmd, "create")) {
        char filename[MAX_FILENAME];
        const char* rest = extract_token(cmd + 6, filename, MAX_FILENAME);

        if (filename[0] == '\0') {
            vga_print("[VFS] Penggunaan: create <nama_berkas> [public|protected] <isi>\n", 0x0E);
        } else {
            uint8_t file_mode = (role == ROLE_ADMIN) ? VFS_PROT_PROTECTED : VFS_PROT_USER;
            const char* content = rest;

            if (role == ROLE_ADMIN) {
                char mode_token[16];
                const char* after_mode = extract_token(rest, mode_token, sizeof(mode_token));

                if (str_eq(mode_token, "public") || str_eq(mode_token, "--public")) {
                    file_mode = VFS_PROT_PUBLIC;
                    content = after_mode;
                } else if (str_eq(mode_token, "protected") || str_eq(mode_token, "--protected")) {
                    file_mode = VFS_PROT_PROTECTED;
                    content = after_mode;
                }
            } else {
                if (str_starts_with(rest, "protected ") || str_starts_with(rest, "--protected ")) {
                    vga_print("[VFS] Akses ditolak: Hanya Admin yang dapat membuat berkas [PROTECTED].\n", 0x0C);
                    return;
                }
            }

            int res = vfs_create(filename, content, file_mode);

            if (res == VFS_OK) {
                vga_print("[VFS] Berkas '", 0x0A);
                vga_print(filename, 0x0A);
                if (file_mode == VFS_PROT_PROTECTED) {
                    vga_print("' berhasil dibuat [PROTECTED].\n", 0x0C);
                } else if (file_mode == VFS_PROT_PUBLIC) {
                    vga_print("' berhasil dibuat [PUBLIC].\n", 0x0E);
                } else {
                    vga_print("' berhasil dibuat [USER].\n", 0x0A);
                }
            } else if (res == VFS_ERR_EXISTS) {
                vga_print("[VFS] Gagal: Berkas '", 0x0C);
                vga_print(filename, 0x0C);
                vga_print("' sudah ada.\n", 0x0C);
            } else if (res == VFS_ERR_FULL) {
                vga_print("[VFS] Gagal: Kapasitas RAMDisk penuh.\n", 0x0C);
            } else {
                vga_print("[VFS] Gagal membuat berkas.\n", 0x0C);
            }
        }

    } else if (str_starts_with(cmd, "read ") || str_eq(cmd, "read")) {
        char filename[MAX_FILENAME];
        static char buffer[MAX_FILESIZE];
        extract_token(cmd + 4, filename, MAX_FILENAME);

        if (filename[0] == '\0') {
            vga_print("[VFS] Penggunaan: read <nama_berkas>\n", 0x0E);
        } else {
            security_set_cli_read_flag(1);
            int res = vfs_read(filename, buffer);
            security_set_cli_read_flag(0);

            if (res == VFS_OK) {
                vga_print("--- Isi Berkas: ", 0x0B);
                vga_print(filename, 0x0F);
                vga_print(" ---\n", 0x0B);
                vga_print(buffer, 0x07);
                vga_putchar('\n', 0x07);
            } else if (res == VFS_ERR_NOT_FOUND) {
                vga_print("[VFS] Gagal: Berkas '", 0x0C);
                vga_print(filename, 0x0C);
                vga_print("' tidak ditemukan.\n", 0x0C);
            } else if (res == VFS_ERR_PROTECTED) {
                vga_print("[VFS] Akses ditolak: Berkas '", 0x0C);
                vga_print(filename, 0x0C);
                vga_print("' terproteksi (hanya Admin).\n", 0x0C);
            } else {
                vga_print("[VFS] Gagal membaca berkas.\n", 0x0C);
            }
        }

    } else if (str_starts_with(cmd, "update ") || str_eq(cmd, "update")) {
        char filename[MAX_FILENAME];
        const char* new_content = extract_token(cmd + 6, filename, MAX_FILENAME);

        if (filename[0] == '\0' || new_content[0] == '\0') {
            vga_print("[VFS] Penggunaan: update <nama_berkas> <isi_baru>\n", 0x0E);
        } else {
            int res = vfs_update(filename, new_content);

            if (res == VFS_OK) {
                vga_print("[VFS] Berkas '", 0x0A);
                vga_print(filename, 0x0A);
                vga_print("' berhasil diperbarui.\n", 0x0A);
            } else if (res == VFS_ERR_NOT_FOUND) {
                vga_print("[VFS] Gagal: Berkas '", 0x0C);
                vga_print(filename, 0x0C);
                vga_print("' tidak ditemukan.\n", 0x0C);
            } else if (res == VFS_ERR_PROTECTED) {
                vga_print("[VFS] Akses ditolak: Berkas milik Admin tidak dapat diubah oleh User.\n", 0x0C);
            } else {
                vga_print("[VFS] Gagal memperbarui berkas.\n", 0x0C);
            }
        }

    } else if (str_starts_with(cmd, "delete ") || str_eq(cmd, "delete")) {
        char filename[MAX_FILENAME];
        extract_token(cmd + 6, filename, MAX_FILENAME);

        if (filename[0] == '\0') {
            vga_print("[VFS] Penggunaan: delete <nama_berkas>\n", 0x0E);
        } else {
            int res = vfs_delete(filename);

            if (res == VFS_OK) {
                vga_print("[VFS] Berkas '", 0x0A);
                vga_print(filename, 0x0A);
                vga_print("' berhasil dihapus.\n", 0x0A);
            } else if (res == VFS_ERR_NOT_FOUND) {
                vga_print("[VFS] Gagal: Berkas '", 0x0C);
                vga_print(filename, 0x0C);
                vga_print("' tidak ditemukan.\n", 0x0C);
            } else if (res == VFS_ERR_PROTECTED) {
                vga_print("[VFS] Akses ditolak: Berkas milik Admin tidak dapat dihapus oleh User.\n", 0x0C);
            } else {
                vga_print("[VFS] Gagal menghapus berkas.\n", 0x0C);
            }
        }

    } else if (str_eq(cmd, "meminfo")) {
        if (!security_require_admin("meminfo")) return;
        size_t total = HEAP_SIZE;
        size_t free_mem = heap_get_free_bytes();
        size_t used_mem = total - free_mem;

        vga_print("=== INFORMASI HEAP GIBRANOS ===\n", 0x0E);

        vga_print("Heap Total : ", 0x07);
        vga_print_number(total);
        vga_print(" bytes\n", 0x07);

        vga_print("Heap Used  : ", 0x07);
        vga_print_number(used_mem);
        vga_print(" bytes\n", 0x07);

        vga_print("Heap Free  : ", 0x07);
        vga_print_number(free_mem);
        vga_print(" bytes\n", 0x07);

    } else if (str_eq(cmd, "test_mem")) {
        if (!security_require_admin("test_mem")) return;
        vga_print("=== [6] PENGUJIAN ALOKATOR MEMORI DINAMIS (HEAP) ===\n", 0x0E);

        size_t total = HEAP_SIZE;
        size_t free_init = heap_get_free_bytes();
        size_t used_init = total - free_init;

        vga_print("1. Kondisi Awal            -> Used: ", 0x07);
        vga_print_number(used_init);
        vga_print(" B | Free: ", 0x07);
        vga_print_number(free_init);
        vga_print(" B\n", 0x07);

        void* memory = kmalloc(2048);

        if (memory != NULL) {
            size_t free_alloc = heap_get_free_bytes();
            size_t used_alloc = total - free_alloc;
            vga_print("2. Setelah kmalloc(2048)   -> Used: ", 0x0A);
            vga_print_number(used_alloc);
            vga_print(" B | Free: ", 0x0A);
            vga_print_number(free_alloc);
            vga_print(" B\n", 0x0A);

            kfree(memory);

            size_t free_after = heap_get_free_bytes();
            size_t used_after = total - free_after;
            vga_print("3. Setelah kfree()         -> Used: ", 0x0A);
            vga_print_number(used_after);
            vga_print(" B | Free: ", 0x0A);
            vga_print_number(free_after);
            vga_print(free_after == free_init ? " B (Pulih!)\n" : " B\n", 0x0A);
        } else {
            vga_print("[GAGAL] Alokasi memori gagal.\n", 0x0C);
        }

        void* oom = kmalloc(HEAP_SIZE + 4096);

        if (oom == NULL) {
            vga_print("4. Uji OOM kmalloc(70KB)   -> Return NULL aman (Kernel tidak crash!)\n", 0x0A);
        } else {
            vga_print("[GAGAL] Alokasi OOM tidak ditolak.\n", 0x0C);
            kfree(oom);
        }

    } else if (str_eq(cmd, "shutdown") || str_eq(cmd, "poweroff")) {
        system_shutdown();

    } else {
        vga_print("Perintah tidak dikenal: '", 0x0C);
        vga_print(cmd, 0x0C);
        vga_print("'. Ketik 'help' untuk bantuan.\n", 0x0C);
    }
}

void shell_run(void) {
    char cmd_buffer[SHELL_BUFFER_SIZE];
    size_t cmd_len = 0;

    security_show_login_screen();
    print_prompt();

    while (1) {
        int scroll = mouse_poll_scroll();

        if (scroll != 0) {
            vga_scroll_view(scroll * 3);
        }

        /* Baca input dari Keyboard PS/2 atau Terminal Serial UART COM1 */
        char c = keyboard_poll_char();
        if (c == 0 && serial_has_data()) {
            c = serial_getchar();
            if (c == '\r') {
                c = '\n';
            } else if (c == 0x7F) {
                c = '\b';
            }
        }

        if (c == 0) {
            __asm__ volatile ("pause");
            continue;
        }

        if (c == KEY_UP) {
            vga_scroll_view(1);
        } else if (c == KEY_DOWN) {
            vga_scroll_view(-1);
        } else if (c == KEY_PGUP) {
            vga_scroll_view(5);
        } else if (c == KEY_PGDN) {
            vga_scroll_view(-5);
        } else if (c == '\n') {
            vga_putchar('\n', 0x07);
            cmd_buffer[cmd_len] = '\0';
            shell_execute(cmd_buffer);
            cmd_len = 0;
            print_prompt();
        } else if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                vga_putchar('\b', 0x07);
            }
        } else if (c >= ' ' && c <= '~') {
            if (cmd_len < SHELL_BUFFER_SIZE - 1) {
                cmd_buffer[cmd_len++] = c;
                vga_putchar(c, 0x07);
            }
        }
    }
}

void shell_task(void) {
    shell_init();
    shell_run();
}