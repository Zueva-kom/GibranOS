#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include <stddef.h>

/* Ukuran maksimal karakter dalam satu baris perintah CLI */
#define SHELL_BUFFER_SIZE 128

/* Fungsi tampilan VGA & utilitas kernel dari src/kernel/main.c */
void vga_clear(uint8_t color);
void vga_putchar(char c, uint8_t color);
void vga_print(const char* str, uint8_t color);
void vga_print_number(size_t number);
void vga_scroll_view(int delta_lines);
void print_banner(void);
void system_shutdown(void);

/* Getter counter thread pekerja untuk pengujian scheduler */
uint32_t kernel_get_worker_a_counter(void);
uint32_t kernel_get_worker_b_counter(void);

/* Inisialisasi state awal CLI Shell */
void shell_init(void);

/* Loop utama CLI interaktif (menerima input Keyboard PS/2 & Serial UART) */
void shell_run(void);

/* Mengeksekusi string perintah internal (Dasar, VFS, Process, Memory, Sync, Security) */
void shell_execute(const char* cmd);

/* Entry point thread CLI Shell yang dijadwalkan oleh Round Robin Scheduler */
void shell_task(void);

#endif
