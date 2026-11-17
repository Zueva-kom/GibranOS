#include "security.h"
#include "../07_Virtual_File_System/vfs.h"
#include "../../drivers/keyboard.h"
#include <stddef.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* Mengambil fungsi & buffer VGA yang sudah ada di main.c */
extern volatile uint16_t* vga_buffer;
extern void vga_clear(uint8_t color);
extern void vga_print(const char* str, uint8_t color);
extern void print_banner(void);

static user_role_t current_role = ROLE_NONE;
static int current_vfs_op_is_cli_read = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static int str_eq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

user_role_t security_get_role(void) {
    return current_role;
}

void security_set_cli_read_flag(int is_cli_read) {
    current_vfs_op_is_cli_read = is_cli_read;
}

/* Kebijakan Akses VFS (otomatis terhubung dengan vfs.c) */
int security_check_access(uint8_t file_protected, int is_write_op) {
    if (current_role == ROLE_ADMIN) {
        return 1;
    }

    if (file_protected == VFS_PROT_PROTECTED) {
        if (is_write_op || current_vfs_op_is_cli_read) {
            return 0; /* Tolak CLI read, update, delete bagi USER */
        }
        return 1; /* Izinkan pembacaan internal oleh Game */
    }

    if (file_protected == VFS_PROT_PUBLIC) {
        if (is_write_op) {
            return 0; /* Tolak update & delete bagi USER pada berkas [PUBLIC] Admin */
        }
        return 1; /* Izinkan READ bagi USER */
    }

    return 1; /* VFS_PROT_USER (0): Izinkan penuh CRUD bagi USER */
}

int security_require_admin(const char* feature_name) {
    if (current_role == ROLE_ADMIN) {
        return 1;
    }
    vga_print("[ACCESS DENIED] Perintah '", 0x0C);
    vga_print(feature_name, 0x0C);
    vga_print("' hanya dapat diakses oleh role ADMIN!\n", 0x0C);
    vga_print("Ketik 'logout' lalu login menggunakan akun 'admin' untuk akses menu 1-8.\n", 0x07);
    return 0;
}

/* ================= UI HALAMAN LOGIN ================= */

static void login_putc(int row, int col, char ch, uint8_t color) {
    if (row >= 0 && row < VGA_HEIGHT && col >= 0 && col < VGA_WIDTH) {
        vga_buffer[row * VGA_WIDTH + col] = ((uint16_t)color << 8) | (uint8_t)ch;
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

    /* KOTAK KIRI: Form Login Utama */
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

    /* KOTAK KANAN ATAS: Daftar Akun */
    login_box(1, 47, 12, 79, 0x0E);
    login_puts(1, 49, "[ DAFTAR AKUN & ROLE ]", 0x0E);

    login_puts(3, 49, "1. ROLE USER (Terbatas)", 0x0B);
    login_puts(4, 52, "User : user", 0x0F);
    login_puts(5, 52, "Pass : user", 0x0F);
    login_puts(6, 52, "Izin : Game, Dasar & File Pub", 0x08);

    login_hline(7, 48, 78, 0x08);

    login_puts(8, 49, "2. ROLE ADMIN (Akses Penuh)", 0x0A);
    login_puts(9, 52, "User : admin", 0x0F);
    login_puts(10, 52, "Pass : ********", 0x0C);
    login_puts(11, 52, "Izin : Semua Menu (1 s/d 8)", 0x08);

    /* KOTAK KANAN BAWAH: Kebijakan Proteksi */
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

void security_show_login_screen(void) {
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
        vga_print("[AUTH] Login berhasil sebagai USER (Akses: Game, Dasar & CRUD Berkas User).\n", 0x0E);
    }
    vga_print("Ketik 'help' untuk melihat menu, atau 'logout' untuk ganti akun.\n\n", 0x07);
}