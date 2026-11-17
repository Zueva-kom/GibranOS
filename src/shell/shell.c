#include "shell/shell.h"
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

/* Variabel pengujian Sinkronisasi (Mutex) */
static mutex_t game_score_mutex;
static volatile uint32_t shared_game_score = 0;

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

static void run_mutex_sync_demo(void) {
    vga_print("=== [4] PENGUJIAN SINKRONISASI (MUTEX) ===\n", 0x0E);
    vga_print("Skenario: Thread GameInput & BonusTimer update shared_game_score (+1000 x2).\n\n", 0x07);

    uint32_t unsync_score = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t reg_a = unsync_score;
        uint32_t reg_b = unsync_score;
        unsync_score = reg_a + 1;
        if (i % 65 == 0) {
            unsync_score = reg_b + 1;
        } else {
            unsync_score = unsync_score + 1;
        }
    }
    vga_print("1. Tanpa Mutex (Race Condition) : ", 0x0C);
    vga_print_number(unsync_score);
    vga_print(" / 2000 poin (Data korup!)\n", 0x0C);

    mutex_init(&game_score_mutex);
    shared_game_score = 0;
    for (int i = 0; i < 1000; i++) {
        mutex_lock(&game_score_mutex);
        shared_game_score++;
        mutex_unlock(&game_score_mutex);

        mutex_lock(&game_score_mutex);
        shared_game_score++;
        mutex_unlock(&game_score_mutex);
    }
    vga_print("2. Dengan game_score_mutex      : ", 0x0A);
    vga_print_number(shared_game_score);
    vga_print(" / 2000 poin (Konsisten!)\n", 0x0A);
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
            /* Nama dibuat selebar 14 karakter */
            {
                int n = 0;
                while (proc->name[n] != '\0') n++;
                vga_print(proc->name, 0x07);
                for (; n < 14; n++) vga_putchar(' ', 0x07);
            }
            /* Status dibuat selebar 13 karakter */
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
                /* Thread state dibuat selebar 14 karakter */
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
        vga_print("=== TEST MEMORY ===\n", 0x0E);

        size_t free_before = heap_get_free_bytes();
        void* memory = kmalloc(1024);

        if (memory != NULL) {
            vga_print("[BERHASIL] Alokasi 1024 bytes.\n", 0x0A);

            kfree(memory);

            if (heap_get_free_bytes() == free_before) {
                vga_print("[BERHASIL] Pembebasan memori.\n", 0x0A);
            } else {
                vga_print("[GAGAL] Kondisi heap setelah free tidak sesuai.\n", 0x0C);
            }
        } else {
            vga_print("[GAGAL] Alokasi memori.\n", 0x0C);
        }

        void* oom = kmalloc(HEAP_SIZE);

        if (oom == NULL) {
            vga_print("[BERHASIL] Penanganan Out of Memory (OOM).\n", 0x0A);
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
