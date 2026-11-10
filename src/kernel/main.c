#include <stdint.h>
#include <stddef.h>
#include "drivers/keyboard.h"
#include "kernel/process/process.h"
#include "kernel/fs/vfs.h"
#include "kernel/game/game.h"
#include "kernel/memory/heap.h"
#include "drivers/serial.h"

#define VGA_WIDTH        80
#define VGA_HEIGHT       25
#define SCROLLBACK_LINES 200
#define CMD_MAX_LEN      128

volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;
static volatile uint16_t history_buffer[SCROLLBACK_LINES][VGA_WIDTH];
int cursor_row = 0;
int cursor_col = 0;
static int scroll_offset = 0;

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
        /* Sembunyikan kursor di luar layar saat sedang melihat riwayat di atas */
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

        /* Konversi karakter UTF-8 block/box-drawing ke CP437 VGA */
        if (ch == 0xE2 && (uint8_t)str[i + 1] != '\0' && (uint8_t)str[i + 2] != '\0') {
            uint8_t b1 = (uint8_t)str[i + 1];
            uint8_t b2 = (uint8_t)str[i + 2];
            if (b1 == 0x96 && b2 == 0x88) {
                ch = 0xDB; /* █ */
                i += 2;
            } else if (b1 == 0x95) {
                if (b2 == 0x90)      ch = 0xCD; /* ═ */
                else if (b2 == 0x91) ch = 0xBA; /* ║ */
                else if (b2 == 0x94) ch = 0xC9; /* ╔ */
                else if (b2 == 0x97) ch = 0xBB; /* ╗ */
                else if (b2 == 0x9A) ch = 0xC8; /* ╚ */
                else if (b2 == 0x9D) ch = 0xBC; /* ╝ */
                else if (b2 == 0xA0) ch = 0xCC; /* ╠ */
                else if (b2 == 0xA3) ch = 0xB9; /* ╣ */
                i += 2;
            } else if (b1 == 0x94 && b2 == 0x84) {
                ch = 0xC4; /* ┄ -> ─ */
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

    // Kirim sinyal ACPI Power Off untuk QEMU, Bochs, dan VirtualBox
    outw(0x604, 0x2000);  // QEMU (versi baru)
    outw(0xB004, 0x2000); // QEMU (versi lama) & Bochs
    outw(0x4004, 0x3400); // VirtualBox

    // Fallback jika dijalankan di hardware tanpa ACPI: hentikan CPU
    vga_print("[SYSTEM] System halted.\n", 0x0C);
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/* Memisahkan argumen pertama (nama berkas) dan sisa string (isi berkas) */
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

static void cli_execute(const char* cmd) {
    // Lewati spasi di awal perintah
    while (*cmd == ' ') {
        cmd++;
    }

    if (*cmd == '\0') {
        return;
    } else if (str_eq(cmd, "help")) {
        vga_print("=== DAFTAR PERINTAH GIBRANOS ===\n", 0x0E);
        vga_print("[Umum & Aplikasi]\n", 0x0B);
        vga_print("  help                     - Menampilkan daftar perintah ini\n", 0x07);
        vga_print("  clear                    - Membersihkan layar terminal VGA\n", 0x07);
        vga_print("  version                  - Menampilkan informasi versi kernel\n", 0x07);
        vga_print("  echo <teks>              - Menampilkan teks ke layar\n", 0x07);
        vga_print("  game                     - Memulai permainan Monster Key-Typing\n", 0x07);
        vga_print("  shutdown                 - Mematikan sistem operasi (Power Off)\n", 0x07);
        vga_print("[File System (RAMDisk VFS)]\n", 0x0B);
        vga_print("  ls                       - Menampilkan daftar berkas di RAMDisk\n", 0x07);
        vga_print("  create <nama> <isi>      - Membuat berkas teks baru\n", 0x07);
        vga_print("  read <nama>              - Membaca isi berkas teks\n", 0x07);
        vga_print("  update <nama> <isi_baru> - Memperbarui isi berkas teks\n", 0x07);
        vga_print("  delete <nama>            - Menghapus berkas teks\n", 0x07);
        vga_print("[Memori, Proses, Sinkronisasi & Keamanan]\n", 0x0B);
        vga_print("  meminfo                  - Menampilkan kapasitas heap (terpakai & bebas)\n", 0x07);
        vga_print("  test_mem                 - Demo alokasi, free, & penanganan Out of Memory\n", 0x07);
    } else if (str_eq(cmd, "clear")) {
        vga_clear(0x07);
    } else if (str_eq(cmd, "version")) {
        vga_print("GibranOS v0.1.0 (x86 32-bit Multiboot Kernel)\n", 0x0A);
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
        const char* content = extract_token(cmd + 6, filename, MAX_FILENAME);

        if (filename[0] == '\0') {
            vga_print("[VFS] Penggunaan: create <nama_berkas> <isi>\n", 0x0E);
        } else {
            int res = vfs_create(filename, content, 0);
            if (res == VFS_OK) {
                vga_print("[VFS] Berkas '", 0x0A);
                vga_print(filename, 0x0A);
                vga_print("' berhasil dibuat.\n", 0x0A);
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
            int res = vfs_read(filename, buffer);
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
                vga_print("' diproteksi.\n", 0x0C);
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
                vga_print("[VFS] Akses ditolak: Berkas sistem '", 0x0C);
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
                vga_print("[VFS] Akses ditolak: Berkas sistem '", 0x0C);
                vga_print(filename, 0x0C);
                vga_print("' terproteksi (hanya Admin).\n", 0x0C);
            } else {
                vga_print("[VFS] Gagal menghapus berkas.\n", 0x0C);
            }
        }
    } else if (str_eq(cmd, "meminfo")) {
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

void kernel_main(void) {
    char cmd_buffer[CMD_MAX_LEN];
    size_t cmd_len = 0;

    vga_clear(0x07);
    heap_init();
    keyboard_init();
    vfs_init();
    serial_init();

    print_banner();
    vga_print("[BOOT] GibranOS Kernel Initialized.\n", 0x0A);
    vga_print("[VFS]  RAMDisk Initialized (words.txt, scores.txt loaded).\n", 0x0A);
    vga_print("[MEM]  Kernel Heap Allocator Ready (64 KB, first-fit).\n", 0x0A);
    vga_print(serial_is_ready() ? "[IO]   Serial UART COM1 Ready (38400 8N1).\n"
                                : "[IO]   Serial UART COM1 tidak terdeteksi.\n", 0x0A);
    vga_print("[IO]   PS/2 Keyboard Driver Ready. Ketik 'help' untuk mulai.\n\n", 0x0A);
    vga_print("GibranOS-CLI $ ", 0x0F);

    while (1) {
        char c = keyboard_getchar();

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
            vga_print("GibranOS-CLI $ ", 0x0F);
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
