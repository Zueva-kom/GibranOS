#include <stdint.h>
#include <stddef.h>
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "kernel/02_Pengelolaan_Proses_Thread/process.h"
#include "kernel/06_Alokator_Memori_Dinamis/heap.h"
#include "kernel/07_Virtual_File_System/vfs.h"
#include "kernel/08_Proteksi_Hak_Akses/security.h"
#include "kernel/game/game.h"
#include "drivers/serial.h"
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/timer.h"
#include "kernel/03_Penjadwalan_CPU_Preemptive/scheduler.h"
#include "kernel/04_Sinkronisasi/mutex.h"

#define VGA_WIDTH        80
#define VGA_HEIGHT       25
#define SCROLLBACK_LINES 200
#define CMD_MAX_LEN      128

volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;
static volatile uint16_t history_buffer[SCROLLBACK_LINES][VGA_WIDTH];
int cursor_row = 0;
int cursor_col = 0;
static int scroll_offset = 0;

/* Counter aktivitas WorkerA dan WorkerB untuk perintah schedtest */
static volatile uint32_t worker_a_counter = 0;
static volatile uint32_t worker_b_counter = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static int get_max_scroll_offset(void) {
    if (cursor_row >= VGA_HEIGHT) {
        return cursor_row - (VGA_HEIGHT - 1);
    }
    return 0;
}

void vga_update_cursor(void) {
    int base_row = get_max_scroll_offset();
    int view_start = base_row - scroll_offset;
    int screen_row = cursor_row - view_start;
    uint16_t pos;

    if (screen_row >= 0 && screen_row < VGA_HEIGHT) {
        pos = (uint16_t)(screen_row * VGA_WIDTH + cursor_col);
    } else {
        pos = (uint16_t)(VGA_WIDTH * VGA_HEIGHT);
    }

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_render(void) {
    int base_row = get_max_scroll_offset();
    int view_start = base_row - scroll_offset;

    for (int row = 0; row < VGA_HEIGHT; row++) {
        int hist_row = view_start + row;
        for (int col = 0; col < VGA_WIDTH; col++) {
            if (hist_row >= 0 && hist_row < SCROLLBACK_LINES) {
                vga_buffer[row * VGA_WIDTH + col] = history_buffer[hist_row][col];
            } else {
                vga_buffer[row * VGA_WIDTH + col] = ((uint16_t)0x07 << 8) | ' ';
            }
        }
    }
    vga_update_cursor();
}

void vga_scroll_view(int delta_lines) {
    int max_offset = get_max_scroll_offset();
    scroll_offset += delta_lines;

    if (scroll_offset < 0) {
        scroll_offset = 0;
    } else if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }

    vga_render();
}

void vga_scroll(uint8_t color) {
    if (cursor_row < SCROLLBACK_LINES) {
        return;
    }

    for (int row = 1; row < SCROLLBACK_LINES; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            history_buffer[row - 1][col] = history_buffer[row][col];
        }
    }

    for (int col = 0; col < VGA_WIDTH; col++) {
        history_buffer[SCROLLBACK_LINES - 1][col] = ((uint16_t)color << 8) | ' ';
    }

    cursor_row = SCROLLBACK_LINES - 1;
}

void vga_clear(uint8_t color) {
    for (int row = 0; row < SCROLLBACK_LINES; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            history_buffer[row][col] = ((uint16_t)color << 8) | ' ';
        }
    }

    cursor_row = 0;
    cursor_col = 0;
    scroll_offset = 0;
    vga_render();
}

void vga_putchar(char c, uint8_t color) {
    scroll_offset = 0;

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
        }
        history_buffer[cursor_row][cursor_col] = ((uint16_t)color << 8) | ' ';
    } else if (c == '\t') {
        int spaces = 4 - (cursor_col % 4);
        for (int i = 0; i < spaces; i++) {
            vga_putchar(' ', color);
        }
        return;
    } else {
        history_buffer[cursor_row][cursor_col] = ((uint16_t)color << 8) | (uint8_t)c;
        cursor_col++;

        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    vga_scroll(0x07);
    vga_render();
}

void vga_print(const char* str, uint8_t color) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        uint8_t ch = (uint8_t)str[i];

        if (ch == 0xE2 && (uint8_t)str[i + 1] != '\0' && (uint8_t)str[i + 2] != '\0') {
            uint8_t b1 = (uint8_t)str[i + 1];
            uint8_t b2 = (uint8_t)str[i + 2];

            if (b1 == 0x96 && b2 == 0x88) {
                ch = 0xDB;
                i += 2;
            } else if (b1 == 0x95) {
                if (b2 == 0x90)      ch = 0xCD;
                else if (b2 == 0x91) ch = 0xBA;
                else if (b2 == 0x94) ch = 0xC9;
                else if (b2 == 0x97) ch = 0xBB;
                else if (b2 == 0x9A) ch = 0xC8;
                else if (b2 == 0x9D) ch = 0xBC;
                else if (b2 == 0xA0) ch = 0xCC;
                else if (b2 == 0xA3) ch = 0xB9;
                i += 2;
            } else if (b1 == 0x94 && b2 == 0x84) {
                ch = 0xC4;
                i += 2;
            }
        }

        vga_putchar((char)ch, color);
    }
}

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

void print_banner(void) {
    vga_print("==================================================\n", 0x0B);
    vga_print("     /\\          ██████╗  ██████╗ ███████╗\n", 0x0B);
    vga_print("    /  \\        ██╔════╝ ██╔═══██╗██╔════╝\n", 0x0B);
    vga_print("   / /\\ \\       ██║  ███╗██║   ██║███████╗\n", 0x0B);
    vga_print("  / /  \\ \\      ██║   ██║██║   ██║╚════██║\n", 0x0B);
    vga_print(" /_/__  \\_\\     ╚██████╔╝╚██████╔╝███████║\n", 0x0B);
    vga_print("   \\_____/       ╚═════╝  ╚═════╝ ╚══════╝\n", 0x0B);
    vga_print("==================================================\n", 0x0B);
}

void system_shutdown(void) {
    vga_print("[SYSTEM] Shutting down GibranOS...\n", 0x0E);

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    vga_print("[SYSTEM] System halted.\n", 0x0C);
    __asm__ volatile ("cli");

    while (1) {
        __asm__ volatile ("hlt");
    }
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

static void vga_print_number(size_t number) {
    char buffer[20];
    int i = 0;

    if (number == 0) {
        vga_putchar('0', 0x07);
        return;
    }

    while (number > 0) {
        buffer[i++] = (char)('0' + (number % 10));
        number /= 10;
    }

    while (i > 0) {
        vga_putchar(buffer[--i], 0x07);
    }
}

/* =========================================================================
 * [4] SINKRONISASI (MUTEX) & [5] DEADLOCK
 * ========================================================================= */
static mutex_t game_score_mutex;
static mutex_t game_vfs_mutex;
static volatile uint32_t shared_game_score = 0;

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

static void run_deadlock_demo(void) {
    mutex_init(&game_score_mutex);
    mutex_init(&game_vfs_mutex);

    vga_print("=== [5] DEMONSTRASI DEADLOCK (CIRCULAR WAIT) ===\n", 0x0C);
    mutex_lock(&game_score_mutex);
    vga_print("[Thread GameInput] Mengunci game_score_mutex -> Menunggu game_vfs_mutex...\n", 0x0E);

    mutex_lock(&game_vfs_mutex);
    vga_print("[Thread AutoSave]  Mengunci game_vfs_mutex   -> Menunggu game_score_mutex...\n", 0x0E);

    vga_print("[DEADLOCK!] Circular Wait terdeteksi! Kedua thread saling mengunci.\n", 0x0C);
    vga_print("Jalankan perintah 'deadlock_fix' untuk melihat solusi Lock Ordering.\n", 0x07);

    mutex_unlock(&game_vfs_mutex);
    mutex_unlock(&game_score_mutex);
}

static void run_deadlock_fix_demo(void) {
    mutex_init(&game_score_mutex);
    mutex_init(&game_vfs_mutex);

    vga_print("=== [5] SOLUSI DEADLOCK (LOCK ORDERING) ===\n", 0x0A);
    vga_print("Aturan Urutan: Selalu kunci (1) game_score_mutex -> lalu (2) game_vfs_mutex.\n", 0x07);

    mutex_lock(&game_score_mutex);
    mutex_lock(&game_vfs_mutex);
    vga_print("[Thread GameInput] Mengunci sesuai urutan (1 -> 2) -> Update skor selesai.\n", 0x0A);
    mutex_unlock(&game_vfs_mutex);
    mutex_unlock(&game_score_mutex);

    mutex_lock(&game_score_mutex);
    mutex_lock(&game_vfs_mutex);
    vga_print("[Thread AutoSave]  Mengunci sesuai urutan (1 -> 2) -> Simpan scores.txt selesai.\n", 0x0A);
    mutex_unlock(&game_vfs_mutex);
    mutex_unlock(&game_score_mutex);

    vga_print("[AMAN] Tidak terjadi Circular Wait. Eksekusi selesai tanpa Deadlock.\n", 0x0A);
}

static void cli_execute(const char* cmd) {
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
        vga_print("=== ROUND ROBIN SCHEDULER TEST ===\n", 0x0E);

        vga_print("WorkerA Counter : ", 0x07);
        vga_print_number(worker_a_counter