#include "shell.h"

// Deklarasikan fungsi dari keyboard driver
extern char keyboard_getchar(void);

void shell_init(void) {
    char buffer[128];
    int buf_idx = 0;

    shell_print("mini-os> ");

    while (1) {
        // Menunggu dan mengambil input karakter dari keyboard
        char c = keyboard_getchar();

        // 1. Jika menekan ENTER
        if (c == '\n') {
            buffer[buf_idx] = '\0';
            shell_putchar('\n');
            
            // Eksekusi perintah shell
            shell_execute(buffer);

            // Reset buffer input
            buf_idx = 0;
            shell_print("\nmini-os> ");
        } 
        // 2. Jika menekan BACKSPACE
        else if (c == '\b') {
            if (buf_idx > 0) {
                buf_idx--;
                shell_putchar('\b'); // Hapus karakter di layar VGA
            }
        } 
        // 3. Karakter biasa (Huruf / Angka / Simbol)
        else {
            if (buf_idx < 127) {
                buffer[buf_idx++] = c;
                shell_putchar(c); // Tampilkan karakter ke layar
            }
        }
    }
}