#include <stdint.h>
#include <stddef.h>

volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;
int cursor_row = 0;
int cursor_col = 0;

void vga_clear(uint8_t color) {
    for (int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = ((uint16_t)color << 8) | ' ';
    }
    cursor_row = 0;
    cursor_col = 0;
}

void vga_print(const char* str, uint8_t color) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            cursor_col = 0;
            cursor_row++;
            continue;
        }
        vga_buffer[cursor_row * 80 + cursor_col] = ((uint16_t)color << 8) | str[i];
        cursor_col++;
        if (cursor_col >= 80) {
            cursor_col = 0;
            cursor_row++;
        }
    }
}

void kernel_main(void) {
    vga_clear(0x07);
    vga_print("==================================================\n", 0x0B);
    vga_print("   ____ _ _                      ___  ____        \n", 0x0B);
    vga_print("  / ___(_) |__  _ __ __ _ _ __  / _ \\/ ___|      \n", 0x0B);
    vga_print(" | |  _| | '_ \\| '__/ _` | '_ \\| | | \\___ \\   \n", 0x0B);
    vga_print(" | |_| | | |_) | | | (_| | | | | |_| |___) |      \n", 0x0B);
    vga_print("  \\____|_|_.__/|_|  \\__,_|_| |_|\\___/|____/    \n", 0x0B);
    vga_print("==================================================\n", 0x0B);
    vga_print("[BOOT] GibranOS Kernel Initialized.\n", 0x0A);
    vga_print("GibranOS-CLI $ ", 0x0F);

    while (1) {
        __asm__ volatile ("hlt");
    }
}