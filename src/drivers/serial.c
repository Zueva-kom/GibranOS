#include "drivers/serial.h"
#include "drivers/io.h"

#define COM1_PORT 0x3F8

#define UART_DATA        0  /* RX/TX buffer (atau divisor low saat DLAB=1) */
#define UART_INT_ENABLE  1  /* interrupt enable (atau divisor high)        */
#define UART_FIFO_CTRL   2
#define UART_LINE_CTRL   3
#define UART_MODEM_CTRL  4
#define UART_LINE_STATUS 5

#define LSR_DATA_READY   0x01
#define LSR_TX_EMPTY     0x20

static int serial_ok = 0;

void serial_init(void) {
    outb(COM1_PORT + UART_INT_ENABLE, 0x00);  /* matikan semua interrupt UART */
    outb(COM1_PORT + UART_LINE_CTRL,  0x80);  /* aktifkan DLAB untuk set baud */
    outb(COM1_PORT + UART_DATA,       0x03);  /* divisor = 3 -> 38400 baud    */
    outb(COM1_PORT + UART_INT_ENABLE, 0x00);
    outb(COM1_PORT + UART_LINE_CTRL,  0x03);  /* 8 bit, tanpa parity, 1 stop  */
    outb(COM1_PORT + UART_FIFO_CTRL,  0xC7);  /* FIFO on, clear, threshold 14 */
    outb(COM1_PORT + UART_MODEM_CTRL, 0x0B);  /* RTS/DSR, OUT2                */

    /* Self-test: mode loopback, kirim 0xAE, harus terbaca kembali */
    outb(COM1_PORT + UART_MODEM_CTRL, 0x1E);
    outb(COM1_PORT + UART_DATA,       0xAE);
    if (inb(COM1_PORT + UART_DATA) != 0xAE) {
        serial_ok = 0;
        return;
    }

    outb(COM1_PORT + UART_MODEM_CTRL, 0x0F);  /* kembali ke mode normal */
    serial_ok = 1;
}

int serial_is_ready(void) {
    return serial_ok;
}

void serial_putchar(char c) {
    if (!serial_ok) {
        return;
    }
    if (c == '\n') {
        serial_putchar('\r');
    }
    while ((inb(COM1_PORT + UART_LINE_STATUS) & LSR_TX_EMPTY) == 0) {
        /* tunggu transmit buffer kosong */
    }
    outb(COM1_PORT + UART_DATA, (uint8_t)c);
}

void serial_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial_putchar(str[i]);
    }
}

int serial_has_data(void) {
    return serial_ok && (inb(COM1_PORT + UART_LINE_STATUS) & LSR_DATA_READY);
}

char serial_getchar(void) {
    while (!serial_has_data()) {
        /* tunggu data masuk */
    }
    return (char)inb(COM1_PORT + UART_DATA);
}
