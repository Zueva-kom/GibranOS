#include <stdint.h>
#include <stddef.h>
#include "drivers/keyboard.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define CMD_MAX_LEN 128

volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;
int cursor_row = 0;
int cursor_col = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

void vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_scroll(uint8_t color) {
    if (cursor_row < VGA_HEIGHT) {
        return;
    }
    for (int row = 1; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga_buffer[(row - 1) * VGA_WIDTH + col] = vga_buffer[row * VGA_WIDTH + col];
        }
    }
    for (int col = 0; col < VGA_WIDTH; col++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = ((uint16_t)color << 8) | ' ';
    }
    cursor_row = VGA_HEIGHT - 1;
}

void vga_clear(uint8_t color) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = ((uint16_t)color << 8) | ' ';
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_update_cursor();
}

void vga_putchar(char c, uint8_t color) {
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
        vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = ((uint16_t)color << 8) | ' ';
    } else if (c == '\t') {
        int spaces = 4 - (cursor_col % 4);
        for (int i = 0; i < spaces; i++) {
            vga_putchar(' ', color);
        }
        return;
    } else {
        vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = ((uint16_t)color << 8) | (uint8_t)c;
        cursor_col++;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }
    vga_scroll(0x07);
    vga_update_cursor();
}

void vga_print(const char* str, uint8_t color) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i], color);
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
    vga_print("   ____ _ _                      ___  ____        \n", 0x0B);
    vga_print("  / ___(_) |__  _ __ __ _ _ __  / _ \\/ ___|      \n", 0x0B);
    vga_print(" | |  _| | '_ \\| '__/ _` | '_ \\| | | \\___ \\   \n", 0x0B);
    vga_print(" | |_| | | |_) | | | (_| | | | | |_| |___) |      \n", 0x0B);
    vga_print("  \\____|_|_.__/|_|  \\__,_|_| |_|\\___/|____/    \n", 0x0B);
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

static void cli_execute(const char* cmd) {
    // Lewati spasi di awal perintah
    while (*cmd == ' ') {
        cmd++;
    }

    if (*cmd == '\0') {
        return;
    } else if (str_eq(cmd, "help")) {
        vga_print("Perintah Internal GibranOS-CLI:\n", 0x0E);
        vga_print("  help       - Menampilkan daftar perintah ini\n", 0x07);
        vga_print("  clear      - Membersihkan layar terminal VGA\n", 0x07);
        vga_print("  version    - Menampilkan informasi versi kernel\n", 0x07);
        vga_print("  echo <txt> - Menampilkan teks ke layar\n", 0x07);
        vga_print("  shutdown   - Mematikan sistem operasi (Power Off)\n", 0x07);
    } else if (str_eq(cmd, "clear")) {
        vga_clear(0x07);
    } else if (str_eq(cmd, "version")) {
        vga_print("GibranOS v0.1.0 (x86 32-bit Multiboot Kernel)\n", 0x0A);
    } else if (str_starts_with(cmd, "echo ")) {
        vga_print(cmd + 5, 0x07);
        vga_putchar('\n', 0x07);
    } else if (str_eq(cmd, "echo")) {
        vga_putchar('\n', 0x07);
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
    keyboard_init();

    print_banner();
    vga_print("[BOOT] GibranOS Kernel Initialized.\n", 0x0A);
    vga_print("[IO]   PS/2 Keyboard Driver Ready. Ketik 'help' untuk mulai.\n\n", 0x0A);
    vga_print("GibranOS-CLI $ ", 0x0F);

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n') {
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
