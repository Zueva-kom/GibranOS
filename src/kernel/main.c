#include <stdint.h>
#include <stddef.h>
#include "drivers/keyboard.h"
#include "kernel/02_Pengelolaan_Proses_Thread/process.h"
#include "kernel/06_Alokator_Memori_Dinamis/heap.h"
#include "kernel/07_Virtual_File_System/vfs.h"
#include "kernel/game/game.h"
#include "drivers/serial.h"
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/timer.h"
#include "03_Penjadwalan_CPU_Preemptive/scheduler.h"

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
        vga_print("=====================================================================================\n", 0x0B);
        vga_print("                         GIBRAN OS - DAFTAR PERINTAH\n", 0x0B);
        vga_print("=====================================================================================\n", 0x0B);

        vga_print("[1] DASAR SISTEM DAN ANTARMUKA PERINTAH\n", 0x0B);
        vga_print("  Bagian ini berisi perintah dasar untuk menggunakan OS.\n", 0x07);
        vga_print("  help                     - Melihat daftar perintah yang tersedia\n", 0x07);
        vga_print("  clear                    - Membersihkan tampilan terminal\n", 0x07);
        vga_print("  version                  - Melihat versi dan informasi kernel\n", 0x07);
        vga_print("  echo <teks>              - Menampilkan teks yang dimasukkan\n", 0x07);
        vga_print("  game                     - Menjalankan permainan Monster Key-Typing\n", 0x07);
        vga_print("  shutdown                 - Mematikan GIBRAN OS dengan aman\n\n", 0x07);
        
        vga_print("[2-3] PENGELOLAAN PROSES, THREAD, DAN PENJADWALAN CPU\n", 0x0B);
        vga_print("  Bagian ini mengelola proses dan thread yang dibuat kernel.\n", 0x07);
        vga_print("  Penjadwal mengatur giliran thread menggunakan konsep Round Robin.\n", 0x07);
        vga_print("  ps                       - Melihat PID, nama, dan status proses/thread\n", 0x07);
        vga_print("  schedtest                - Melihat aktivitas counter WorkerA dan B\n\n", 0x07);

        vga_print("[4] SINKRONISASI DAN MUTEX\n", 0x0B);
        vga_print("  Bagian ini untuk pengaturan akses bersama antar-thread.\n", 0x07);
        vga_print("  Mutex digunakan untuk melindungi data saat diakses bersamaan.\n", 0x07);
        vga_print("  mutex_test               - Demo mutex (belum diimplementasikan)\n\n", 0x07);

        vga_print("[5] DEADLOCK DAN PENANGANANNYA\n", 0x0B);
        vga_print("  Bagian ini untuk mengatasi kondisi thread saling menunggu sumber daya.\n", 0x07);
        vga_print("  Pembahasan mencakup penyebab dan cara menghindari deadlock.\n", 0x07);
        vga_print("  deadlock                 - Demo kondisi deadlock (belum aktif)\n", 0x07);
        vga_print("  deadlock_fix             - Demo penanganan deadlock (belum aktif)\n\n", 0x07);

        vga_print("[6] PENGELOLAAN MEMORI DINAMIS\n", 0x0B);
        vga_print("  Bagian ini mengelola alokasi dan pembebasan memori heap kernel.\n", 0x07);
        vga_print("  meminfo                  - Melihat total, terpakai, dan sisa heap\n", 0x07);
        vga_print("  test_mem                 - Menguji alokasi, free, dan kondisi OOM\n\n", 0x07);

        vga_print("[7] SISTEM BERKAS VIRTUAL (RAMDISK)\n", 0x0B);
        vga_print("  Bagian ini mengelola berkas di media penyimpanan berbasis RAM.\n", 0x07);
        vga_print("  ls                       - Melihat daftar berkas yang tersimpan\n", 0x07);
        vga_print("  create <nama> <isi>      - Membuat berkas baru beserta isinya\n", 0x07);
        vga_print("  read <nama>              - Membaca dan menampilkan isi berkas\n", 0x07);
        vga_print("  update <nama> <isi_baru> - Mengubah isi berkas yang tersimpan\n", 0x07);
        vga_print("  delete <nama>            - Menghapus berkas dari RAMDisk\n\n", 0x07);

        vga_print("[8] PROTEKSI DAN HAK AKSES\n", 0x0B);
        vga_print("  Bagian ini mengelolah pembatasan akses terhadap sumber daya OS.\n", 0x07);
        vga_print("  Hak akses menentukan operasi yang boleh dilakukan pengguna.\n", 0x07);
        vga_print("  access_test              - Demo pemeriksaan hak akses (belum aktif)\n\n", 0x07);
        vga_print("============================================================================\n", 0x0B);
    } else if (str_eq(cmd, "schedtest")) {
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
        vga_print("[SINKRONISASI] Fitur mutex belum diimplementasikan.\n", 0x0E);
        vga_print("Perintah dikenali, tetapi pengujian belum dapat dijalankan.\n", 0x07);
    } else if (str_eq(cmd, "deadlock")) {
        vga_print("[DEADLOCK] Demonstrasi deadlock belum diimplementasikan.\n", 0x0E);
        vga_print("Perintah dikenali, tetapi pengujian belum dapat dijalankan.\n", 0x07);
    } else if (str_eq(cmd, "deadlock_fix")) {
        vga_print("[DEADLOCK] Fitur penanganan deadlock belum diimplementasikan.\n", 0x0E);
        vga_print("Perintah dikenali, tetapi pengujian belum dapat dijalankan.\n", 0x07);
    } else if (str_eq(cmd, "access_test")) {
        vga_print("[PROTEKSI] Pengujian hak akses belum diimplementasikan.\n", 0x0E);
        vga_print("Perintah dikenali, tetapi pengujian belum dapat dijalankan.\n", 0x07);
    } else if (str_eq(cmd, "ps")) {
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

    vga_print("[IO] PS/2 Keyboard Driver Ready. Ketik 'help' untuk mulai.\n\n", 0x0A);
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


void kernel_main(void) {
    __asm__ volatile ("cli");

    vga_clear(0x07);

    gdt_init();
    idt_init();
    timer_init();

    heap_init();

    /* Inisialisasi scheduler dan proses */
    scheduler_init();
    // process_test();
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

