#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stddef.h>

#define KB_DATA_PORT   0x60
#define KB_STATUS_PORT 0x64

/* Kode tombol khusus untuk navigasi / scrolling terminal */
#define KEY_UP    0x11
#define KEY_DOWN  0x12
#define KEY_PGUP  0x13
#define KEY_PGDN  0x14

void keyboard_init(void);
char keyboard_poll_char(void);
char keyboard_getchar(void);

#endif
