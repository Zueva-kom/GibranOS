
#include <stdint.h>
#include "io.h"
#include "mouse.h"

#define MOUSE_DATA_PORT   0x60
#define MOUSE_STATUS_PORT 0x64

static uint8_t packet[4];
static uint8_t packet_index = 0;
static uint8_t packet_size = 3;
static int mouse_ready = 0;

static int wait_input_clear(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(MOUSE_STATUS_PORT) & 0x02)) {
            return 1;
        }
    }
    return 0;
}

static int mouse_read(uint8_t* data) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(MOUSE_STATUS_PORT);

        if ((status & 0x21) == 0x21) {
            *data = inb(MOUSE_DATA_PORT);
            return 1;
        }
    }
    return 0;
}

static int mouse_write(uint8_t value) {
    if (!wait_input_clear()) return 0;

    outb(MOUSE_STATUS_PORT, 0xD4);

    if (!wait_input_clear()) return 0;

    outb(MOUSE_DATA_PORT, value);

    uint8_t response;
    return mouse_read(&response) && response == 0xFA;
}

static int mouse_set_rate(uint8_t rate) {
    return mouse_write(0xF3) && mouse_write(rate);
}

void mouse_init(void) {
    mouse_ready = 0;
    packet_index = 0;

    if (!wait_input_clear()) return;
    outb(MOUSE_STATUS_PORT, 0xA8);

    if (!mouse_write(0xF6)) return;

    if (!mouse_set_rate(200)) return;
    if (!mouse_set_rate(100)) return;
    if (!mouse_set_rate(80)) return;

    if (!mouse_write(0xF2)) return;

    uint8_t mouse_id;
    if (!mouse_read(&mouse_id)) return;

    if (mouse_id == 3 || mouse_id == 4) {
        packet_size = 4;
    } else {
        packet_size = 3;
    }

    if (!mouse_write(0xF4)) return;

    mouse_ready = 1;
}

int mouse_poll_scroll(void) {
    if (!mouse_ready || packet_size != 4) {
        return 0;
    }

    while (inb(MOUSE_STATUS_PORT) & 0x01) {
        uint8_t status = inb(MOUSE_STATUS_PORT);

        if (!(status & 0x20)) {
            break;
        }

        uint8_t data = inb(MOUSE_DATA_PORT);

        if (packet_index == 0 && !(data & 0x08)) {
            continue;
        }

        packet[packet_index++] = data;

        if (packet_index == packet_size) {
            packet_index = 0;

            int8_t wheel = packet[3] & 0x0F;

            if (wheel & 0x08) {
                wheel |= (int8_t)0xF0;
            }

            if (wheel < 0) return 1;
            if (wheel > 0) return -1;
        }
    }

    return 0;
}
