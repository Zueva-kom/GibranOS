#include <stdint.h>
#include <stddef.h>
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "kernel/02_Pengelolaan_Proses_Thread/process.h"
#include "kernel/06_Alokator_Memori_Dinamis/heap.h"
#include "kernel/07_Virtual_File_System/vfs.h"
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
 * [8] PROTEKSI & HAK AKSES (HALAMAN LOGIN BERKOTAK + PEMBATASAN ROLE)
 * ========================================================================= */
typedef enum {
    ROLE_NONE  = 0,
    ROLE_USER  = 1,
    ROLE_ADMIN = 2
} user_role_t;

static user_role_t current_role = ROLE_NONE;
static int current_vfs_op_is_cli_read = 0;

/* Menimpa fungsi weak di vfs.c:
 * - Game internal (current_vfs_op_is_cli_read == 0) boleh membaca words.txt/scores.txt
 * - CLI read/update/delete pada berkas [PROTECTED] HANYA diizinkan untuk ADMIN
 */
int security_check_access(uint8_t file_protected, int is_write_op) {
    if (file_protected) {
        if (is_write_op || current_vfs_op_is_cli_read) {
            return (current_role == ROLE_ADMIN) ? 1 : 0;
        }
    }
    return 1;
}

static int require_admin_role(const char* feature_name) {
    if (current_role == ROLE_ADMIN) {
        return 1;
    }
    vga_print("[ACCESS DENIED] Perintah '", 0x0C);
    vga_print(feature_name, 0x0C);
    vga_print("' hanya dapat diakses oleh role ADMIN!\n", 0x0C);
    vga_print("Ketik 'logout' lalu login menggunakan akun 'admin' untuk akses menu 1-8.\n", 0x07);
    return 0;
}

static void login_putc(int row, int col, char ch, uint8_t color) {
    if (row >= 0 && row < VGA_HEIGHT && col >= 0 && col < VGA_WIDTH) {
        uint16_t cell = ((uint16_t)color << 8) | (uint8_t)ch;
        vga_buffer[row * VGA_WIDTH + col] = cell;
        history_buffer[row][col] = cell;
    }
}

static void login_puts(int row, int col, const char* str, uint8_t color) {
    int c = col;
    while (*str != '\0' && c < VGA_WIDTH) {
        login_putc(row, c++, *str++, color);
    }
}

static void login_hline(int row, int c1, int c2, uint8_t color) {
    for (int c = c1; c <= c2; c++) {
        login_putc(row, c, (char)0xC4, color); /* ─ */
    }
}

static void login_box(int r1, int c1, int r2, int c2, uint8_t color) {
    login_hline(r1, c1 + 1, c2 - 1, color);
    login_hline(r2, c1 + 1, c2 - 1, color);
    for (int r = r1 + 1; r < r2; r++) {
        login_putc(r, c1, (char)0xB3, color); /* │ */
        login_putc(r, c2, (char)0xB3, color); /* │ */
    }
    login_putc(r1, c1, (char)0xDA, color); /* ┌ */
    login_putc(r1, c2, (char)0xBF, color); /* ┐ */
    login_putc(r2, c1, (char)0xC0, color); /* └ */
    login_putc(r2, c2, (char)0xD9, color); /* ┘ */
}

static void login_read_field(int row, int col, int field_width, char* out_buf, size_t max_len, int is_password) {
    size_t len = 0;
    while (1) {
        for (int i = 0; i < field_width; i++) {
            if ((size_t)i < len) {
                login_putc(row, col + i, is_password ? '*' : out_buf[i], 0x0F);
            } else if ((size_t)i == len) {
                login_putc(row, col + i, '_', 0x0A);
            } else {
                login_putc(row, col + i, ' ', 0x07);
            }
        }

        char c = keyboard_poll_char();
        if (c == 0) {
            __asm__ volatile ("pause");
            continue;
        }

        if (c == '\n') {
            if (len < (size_t)field_width) {
                login_putc(row, col + (int)len, ' ', 0x07);
            }
            out_buf[len] = '\0';
            return;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                out_buf[len] = '\0';
            }
        } else if (c >= ' ' && c <= '~') {
            if (len < max_len - 1 && len < (size_t)(field_width - 1)) {
                out_buf[len++] = c;
                out_buf[len] = '\0';
            }
        }
    }
}

static void render_login_ui(const char* status_msg, uint8_t status_color) {
    vga_clear(0x07);

    /* Sembunyikan kursor hardware agar menggunakan kursor '_' di dalam kotak */
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)((VGA_WIDTH * VGA_HEIGHT) & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(((VGA_WIDTH * VGA_HEIGHT) >> 8) & 0xFF));

    /* Baris 0: Top Bar Biru */
    for (int c = 0; c < VGA_WIDTH; c++) {
        login_putc(0, c, ' ', 0x1F);
    }
    login_puts(0, 1, "GIBRANOS SECURITY GATEWAY", 0x1E);
    login_puts(0, 28, "| Ketentuan [8]: Proteksi & Hak Akses OS", 0x1F);

    /* KOTAK KIRI (Kolom 0..46, Baris 1..23): Form Login Utama */
    login_box(1, 0, 23, 46, 0x0B);
    login_puts(1, 2, "[ LOGIN SISTEM GIBRANOS ]", 0x0B);

    login_puts(3, 13, "\xDB\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB", 0x0B);
    login_puts(4, 13, "\xDB\xDB      \xDB\xDB  \xDB\xDB  \xDB\xDB    ", 0x0B);
    login_puts(5, 13, "\xDB\xDB \xDB\xDB\xDB  \xDB\xDB  \xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB", 0x0B);
    login_puts(6, 13, "\xDB\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB", 0x0B);
    login_puts(7, 13, "GIBRANOS KERNEL v0.1.2", 0x0F);
    login_hline(8, 2, 44, 0x08);

    login_puts(10, 4, "Silakan masuk untuk menentukan hak akses:", 0x07);
    login_puts(12, 4, "Username :", 0x0F);
    login_puts(12, 15, "[                    ]", 0x08);
    login_puts(14, 4, "Password :", 0x0F);
    login_puts(14, 15, "[                    ]", 0x08);

    login_hline(17, 2, 44, 0x08);
    login_puts(18, 4, "Status   :", 0x07);
    login_puts(19, 4, status_msg, status_color);

    login_puts(22, 4, "[ENTER] Konfirmasi Input", 0x0A);
    login_puts(22, 30, "[BKSP] Hapus", 0x08);

    /* KOTAK KANAN ATAS (Kolom 47..79, Baris 1..12): Daftar Akun */
    login_box(1, 47, 12, 79, 0x0E);
    login_puts(1, 49, "[ DAFTAR AKUN & ROLE ]", 0x0E);

    login_puts(3, 49, "1. ROLE USER (Terbatas)", 0x0B);
    login_puts(4, 52, "User : user", 0x0F);
    login_puts(5, 52, "Pass : user", 0x0F);
    login_puts(6, 52, "Izin : Game, Dasar & File Pub", 0x08);

    login_hline(7, 48, 78, 0x08);

    login_puts(8, 49, "2. ROLE ADMIN (Akses Penuh)", 0x0A);
    login_puts(9, 52, "User : admin", 0x0F);
    login_puts(10, 52, "Pass : admin123", 0x0F);
    login_puts(11, 52, "Izin : Semua Menu (1 s/d 8)", 0x08);

    /* KOTAK KANAN BAWAH (Kolom 47..79, Baris 13..23): Kebijakan Proteksi */
    login_box(13, 47, 23, 79, 0x0A);
    login_puts(13, 49, "[ MATRIKS KEBIJAKAN OS ]", 0x0A);

    login_puts(15, 49, "Fitur / Menu      USER  ADMIN", 0x07);
    login_hline(16, 48, 78, 0x08);
    login_puts(17, 49, "[1] Game & Dasar   OK    OK", 0x07);
    login_puts(18, 49, "[2-3] Proc/Sched  LOCK   OK", 0x07);
    login_puts(19, 49, "[4-5] Mutex/Dead  LOCK   OK", 0x07);
    login_puts(20, 49, "[6] Heap Memori   LOCK   OK", 0x07);
    login_puts(21, 49, "[7-8] VFS File    PUB    ALL", 0x07);

    login_puts(24, 1, "Ketik 'logout' di CLI kapan saja untuk kembali ke Halaman Login ini.", 0x08);
}

static void show_login_screen(void) {
    char user_buf[20];
    char pass_buf[20];
    const char* status = "Menunggu input Username & Password...";
    uint8_t status_col = 0x0E;
    current_role = ROLE_NONE;

    while (current_role == ROLE_NONE) {
        render_login_ui(status, status_col);
        login_read_field(12, 17, 18, user_buf, sizeof(user_buf), 0);
        login_read_field(14, 17, 18, pass_buf, sizeof(pass_buf), 1);

        if (str_eq(user_buf, "user") && str_eq(pass_buf, "user")) {
            current_role = ROLE_USER;
        } else if (str_eq(user_buf, "admin") && str_eq(pass_buf, "admin123")) {
            current_role = ROLE_ADMIN;
        } else {
            status = "[DITOLAK] Username/Password salah!";
            status_col = 0x0C;
        }
    }

    vga_clear(0x07);
    print_banner();
    if (current_role == ROLE_ADMIN) {
        vga_print("[AUTH] Login berhasil sebagai ADMINISTRATOR (Akses Penuh Menu 1-8).\n", 0x0A);
    } else {
        vga_print("[AUTH] Login berhasil sebagai USER (Akses: Game, Dasar & CRUD Berkas Public).\n", 0x0E);
    }
    vga_print("Ketik 'help' untuk melihat menu, atau 'logout' untuk ganti akun.\n\n", 0x07);
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
            unsync_score = reg_b + 1; /* Race condition (lost update) */
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
                  current_role == ROLE_ADMIN ? 0x0B : 0x08);
        vga_print("  ps                       - Melihat PID, nama, dan status proses/thread\n", 0x07);
        vga_print("  schedtest                - Melihat aktivitas counter WorkerA dan B\n\n", 0x07);

        vga_print("[4] SINKRONISASI DAN MUTEX (ADMIN ONLY)\n",
                  current_role == ROLE_ADMIN ? 0x0B : 0x08);
        vga_print("  mutex_test               - Demo perbandingan Race Condition vs Mutex\n\n", 0x07);

        vga_print("[5] DEADLOCK DAN PENANGANANNYA (ADMIN ONLY)\n",
                  current_role == ROLE_ADMIN ? 0x0B : 0x08);
        vga_print("  deadlock                 - Demo kondisi deadlock (Circular Wait)\n", 0x07);
        vga_print("  deadlock_fix             - Demo penanganan deadlock (Lock Ordering)\n\n", 0x07);

        vga_print("[6] PENGELOLAAN MEMORI DINAMIS (ADMIN ONLY)\n",
                  current_role == ROLE_ADMIN ? 0x0B : 0x08);
        vga_print("  meminfo                  - Melihat total, terpakai, dan sisa heap\n", 0x07);
        vga_print("  test_mem                 - Menguji alokasi, free, dan kondisi OOM\n\n", 0x07);

        vga_print("[7] SISTEM BERKAS VIRTUAL / RAMDISK (PUBLIC: ALL | PROTECTED: ADMIN ONLY)\n", 0x0B);
        vga_print("  ls                       - Melihat daftar berkas yang tersimpan\n", 0x07);
        vga_print("  create <nama> <isi>      - Buat berkas (Admin: [PROTECTED] | User: [PUBLIC])\n", 0x07);
        vga_print("  create <nama> --public <isi> - Admin buat berkas agar berstatus [PUBLIC]\n", 0x07);
        vga_print("  read <nama>              - Membaca isi berkas (Protected: Admin Only)\n", 0x07);
        vga_print("  update <nama> <isi_baru> - Mengubah isi berkas (Protected: Admin Only)\n", 0x07);
        vga_print("  delete <nama>            - Menghapus berkas (Protected: Admin Only)\n\n", 0x07);

        vga_print("[8] PROTEKSI DAN HAK AKSES\n", 0x0B);
        vga_print("  access_test              - Cek status role aktif & kebijakan hak akses\n", 0x07);
        vga_print("=====================================================================================\n", 0x0B);

    } else if (str_eq(cmd, "logout")) {
        show_login_screen();

    } else if (str_eq(cmd, "schedtest")) {
        if (!require_admin_role("schedtest")) return;
        vga_print("=== ROUND ROBIN SCHEDULER TEST ===\n", 0x0E);

        vga_print("WorkerA Counter : ", 0x07);
        vga_print_number(worker_a_counter);
        vga_putchar('\n', 0x07);

        vga_print("WorkerB Counter : ", 0x07);
        vga_print_number(worker_b_counter);
        vga_putchar('\n', 0x07);

        if (worker_a_counter > 0 && worker_b_counter > 0) {
            vga_print("Result          : BOTH WORKERS HAVE RUN\n", 0x0A);
        } else {
            vga_print("Result          : ONE OR BOTH WORKERS NOT RUN YET\n", 0x0E);
        }

        vga_print("Scheduler       : ROUND ROBIN (configured)\n", 0x07);

    } else if (str_eq(cmd, "mutex_test")) {
        if (!require_admin_role("mutex_test")) return;
        run_mutex_sync_demo();

    } else if (str_eq(cmd, "deadlock")) {
        if (!require_admin_role("deadlock")) return;
        run_deadlock_demo();

    } else if (str_eq(cmd, "deadlock_fix")) {
        if (!require_admin_role("deadlock_fix")) return;
        run_deadlock_fix_demo();

    } else if (str_eq(cmd, "access_test")) {
        vga_print("=== [8] STATUS PROTEKSI & HAK AKSES ===\n", 0x0E);
        vga_print("Peran Aktif : ", 0x07);
        vga_print(current_role == ROLE_ADMIN ? "ADMIN (Akses Penuh Menu 1-8 & Berkas Protected)\n"
                                             : "USER (Akses Game, Dasar & CRUD Berkas Public)\n",
                  current_role == ROLE_ADMIN ? 0x0A : 0x0E);
        vga_print("Gunakan perintah 'logout' untuk berpindah antara akun 'user' dan 'admin'.\n", 0x07);

    } else if (str_eq(cmd, "ps")) {
        if (!require_admin_role("ps")) return;
        vga_print("=== PROCESS & THREAD LIST ===\n", 0x0E);
        vga_print("Jumlah proses aktif: ", 0x07);
        vga_print_number(process_get_count());
        vga_putchar('\n', 0x07);
        vga_print("PID  NAME                 STATE  TID  THREAD STATE\n", 0x0B);

        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            pcb_t* proc = process_get_by_index(i);
            if (proc == NULL) {
                continue;
            }

            vga_print_number(proc->pid);
            vga_print("    ", 0x07);
            vga_print(proc->name, 0x07);
            vga_print("    ", 0x07);
            vga_print(process_state_to_str(proc->state), 0x07);
            vga_print("    ", 0x07);

            if (proc->main_thread != NULL) {
                vga_print_number(proc->main_thread->tid);
                vga_print("    ", 0x07);
                vga_print(process_state_to_str(proc->main_thread->state), 0x07);
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
            vga_print("[VFS] Penggunaan: create <nama_berkas> [--public] <isi>\n", 0x0E);
        } else {
            /* Default otomatis: ADMIN -> [PROTECTED] (1), USER -> [PUBLIC] (0) */
            uint8_t is_prot = (current_role == ROLE_ADMIN) ? 1 : 0;
            const char* content = rest;

            /* Opsi tambahan jika Admin ingin berkasnya berstatus [PUBLIC] */
            if (str_starts_with(rest, "--public ")) {
                is_prot = 0;
                content = rest + 9;
                while (*content == ' ') content++;
            } else if (str_eq(rest, "--public")) {
                is_prot = 0;
                content = "";
            } else if (str_starts_with(rest, "--protected ")) {
                if (current_role != ROLE_ADMIN) {
                    vga_print("[VFS] Akses ditolak: Hanya Admin yang dapat membuat berkas [PROTECTED].\n", 0x0C);
                    return;
                }
                is_prot = 1;
                content = rest + 12;
                while (*content == ' ') content++;
            }

            int res = vfs_create(filename, content, is_prot);

            if (res == VFS_OK) {
                vga_print("[VFS] Berkas '", 0x0A);
                vga_print(filename, 0x0A);
                vga_print(is_prot ? "' berhasil dibuat [PROTECTED].\n" : "' berhasil dibuat [PUBLIC].\n",
                          is_prot ? 0x0E : 0x0A);
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
            /* Tandai bahwa ini pembacaan dari CLI agar berkas [PROTECTED] dicek hak aksesnya */
            current_vfs_op_is_cli_read = 1;
            int res = vfs_read(filename, buffer);
            current_vfs_op_is_cli_read = 0;

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
                vga_print("[VFS] Akses ditolak: Berkas '", 0x0C);
                vga_print(filename, 0x0C);
                vga_print("' terproteksi (hanya Admin).\n", 0x0C);
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
                vga_print("[VFS] Akses ditolak: Berkas '", 0x0C);
                vga_print(filename, 0x0C);
                vga_print("' terproteksi (hanya Admin).\n", 0x0C);
            } else {
                vga_print("[VFS] Gagal menghapus berkas.\n", 0x0C);
            }
        }

    } else if (str_eq(cmd, "meminfo")) {
        if (!require_admin_role("meminfo")) return;
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
        if (!require_admin_role("test_mem")) return;
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

/* Thread Worker A */
static void worker_a_task(void) {
    while (1) {
        worker_a_counter++;
    }
}

/* Thread Worker B */
static void worker_b_task(void) {
    while (1) {
        worker_b_counter++;
    }
}

/* CLI tetap berjalan sebagai thread */
static void shell_task(void) {
    char cmd_buffer[CMD_MAX_LEN];
    size_t cmd_len = 0;

    /* Tampilkan halaman login berkotak pertama kali setelah booting selesai */
    show_login_screen();

    vga_print(current_role == ROLE_ADMIN ? "GibranOS-CLI [ADMIN] $ " : "GibranOS-CLI [USER] $ ",
              current_role == ROLE_ADMIN ? 0x0A : 0x0B);

    while (1) {
        int scroll = mouse_poll_scroll();

        if (scroll != 0) {
            vga_scroll_view(scroll * 3);
        }

        char c = keyboard_poll_char();

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
            cli_execute(cmd_buffer);
            cmd_len = 0;
            vga_print(current_role == ROLE_ADMIN ? "GibranOS-CLI [ADMIN] $ " : "GibranOS-CLI [USER] $ ",
                      current_role == ROLE_ADMIN ? 0x0A : 0x0B);
        } else if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                vga_putchar('\b', 0x07);
            }
        } else if (c >= ' ' && c <= '~') {
            if (cmd_len < CMD_MAX_LEN - 1) {
                cmd_buffer[cmd_len++] = c;
                vga_putchar(c, 0x07);
            }
        }
    }
}

void kernel_main(void) {
    __asm__ volatile ("cli");

    vga_clear(0x07);

    gdt_init();
    idt_init();
    timer_init();

    heap_init();

    /* Inisialisasi scheduler dan proses */
    scheduler_init();
    process_init();

    /* Membuat dua worker dan satu thread CLI */
    pcb_t* worker_a = process_create(
        "WorkerA", worker_a_task, 1, PRIVILEGE_KERNEL
    );

    pcb_t* worker_b = process_create(
        "WorkerB", worker_b_task, 1, PRIVILEGE_KERNEL
    );

    pcb_t* shell = process_create(
        "Shell", shell_task, 1, PRIVILEGE_KERNEL
    );

    keyboard_init();
    mouse_init();
    vfs_init();
    serial_init();

    print_banner();

    vga_print("[BOOT] GibranOS Kernel Initialized.\n", 0x0A);
    vga_print("[VFS]  RAMDisk Initialized (words.txt, scores.txt loaded).\n", 0x0A);
    vga_print("[MEM]  Kernel Heap Allocator Ready (64 KB, first-fit).\n", 0x0A);
    vga_print("[PROC] Process Management Ready.\n", 0x0A);
    vga_print("[SCHED] Round Robin Scheduler Ready (Quantum 5).\n", 0x0A);

    vga_print(
        worker_a != NULL && worker_b != NULL && shell != NULL
        ? "[THREAD] WorkerA, WorkerB, Shell berhasil dibuat.\n"
        : "[ERROR] Gagal membuat salah satu thread.\n",
        worker_a != NULL && worker_b != NULL && shell != NULL
        ? 0x0A : 0x0C
    );

    vga_print(
        serial_is_ready()
        ? "[IO] Serial UART COM1 Ready (38400 8N1).\n"
        : "[IO] Serial UART COM1 tidak terdeteksi.\n",
        0x0A
    );

    if (worker_a == NULL || worker_b == NULL || shell == NULL) {
        vga_print("[ERROR] Scheduler tidak diaktifkan.\n", 0x0C);

        while (1) {
            __asm__ volatile ("hlt");
        }
    }

    /* Aktifkan interrupt setelah semua thread siap */
    __asm__ volatile ("sti");

    /* Kernel utama menjadi idle */
    while (1) {
        __asm__ volatile ("hlt");
    }
}