#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

/*
 * Driver Serial UART 16550 (COM1, port I/O 0x3F8), 38400 baud, 8N1.
 * Dengan QEMU: jalankan dengan  -serial stdio  agar output tampil di terminal host.
 */

void serial_init(void);                 /* inisialisasi + loopback self-test */
int  serial_is_ready(void);             /* 1 jika UART lolos self-test       */
void serial_putchar(char c);            /* '\n' otomatis dikirim sebagai "\r\n" */
void serial_print(const char* str);
int  serial_has_data(void);             /* 1 jika ada byte masuk (RX)        */
char serial_getchar(void);              /* blocking sampai ada byte masuk    */

#endif /* SERIAL_H */
