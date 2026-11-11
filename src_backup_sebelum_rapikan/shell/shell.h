#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include <stddef.h>

// Ukuran maksimal karakter dalam satu baris perintah CLI
#define SHELL_BUFFER_SIZE 128

// Inisialisasi state awal CLI Shell
void shell_init(void);

// Loop utama CLI interaktif (membaca input keyboard/serial)
void shell_run(void);

// Mengeksekusi string perintah internal (VFS, Process, Memory, Sync)
void shell_execute(char* cmd);

#endif