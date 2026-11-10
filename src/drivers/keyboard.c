#include "keyboard.h"

static uint8_t shift_pressed = 0;
static uint8_t caps_lock     = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Tabel Scancode Set 1 (US QWERTY) - Normal (Tanpa Shift)
static const char scancode_normal[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, // 29: Left Control
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, // 42: Left Shift
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, // 54: Right Shift
    '*',
    0, // 56: Left Alt
    ' ', // 57: Spacebar
    0  // Sisanya (CapsLock, F1-F12, dll.) bernilai 0
};

// Tabel Scancode Set 1 (US QWERTY) - Dengan Shift
static const char scancode_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, // 29: Left Control
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, // 42: Left Shift
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, // 54: Right Shift
    '*',
    0, // 56: Left Alt
    ' ', // 57: Spacebar
    0
};

void keyboard_init(void) {
    shift_pressed = 0;
    caps_lock = 0;
    // Kosongkan sisa data di buffer controller PS/2 jika ada
    while (inb(KB_STATUS_PORT) & 0x01) {
        (void)inb(KB_DATA_PORT);
    }
}

char keyboard_poll_char(void) {
    // Cek bit 0 pada port status (0x64): 1 berarti output buffer penuh (ada data)
    if (!(inb(KB_STATUS_PORT) & 0x01)) {
        return 0;
    }

    uint8_t scancode = inb(KB_DATA_PORT);

    // Abaikan prefix extended scancode (0xE0)
    if (scancode == 0xE0) {
        return 0;
    }

    // Jika bit ke-7 bernilai 1 (0x80), berarti tombol dilepas (Key Release)
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) { // Left / Right Shift dilepas
            shift_pressed = 0;
        }
        return 0;
    }

    // Tangani tombol modifier saat ditekan (Key Press)
    if (scancode == 0x2A || scancode == 0x36) { // Left / Right Shift ditekan
        shift_pressed = 1;
        return 0;
    }
    if (scancode == 0x3A) { // Caps Lock ditekan
        caps_lock = !caps_lock;
        return 0;
    }

    // Tombol navigasi untuk scroll layar (Arrow Up/Down & Page Up/Down)
    if (scancode == 0x48) return KEY_UP;
    if (scancode == 0x50) return KEY_DOWN;
    if (scancode == 0x49) return KEY_PGUP;
    if (scancode == 0x51) return KEY_PGDN;

    if (scancode >= 128) {
        return 0;
    }

    char c = shift_pressed ? scancode_shift[scancode] : scancode_normal[scancode];

    // Terapkan efek Caps Lock khusus pada huruf alfabet (a-z / A-Z)
    if (caps_lock) {
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        } else if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }

    return c;
}

char keyboard_getchar(void) {
    char c = 0;
    while ((c = keyboard_poll_char()) == 0) {
        __asm__ volatile ("pause");
    }
    return c;
}
