#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stddef.h>

#define KB_DATA_PORT   0x60
#define KB_STATUS_PORT 0x64

void keyboard_init(void);
char keyboard_poll_char(void);
char keyboard_getchar(void);

#endif
