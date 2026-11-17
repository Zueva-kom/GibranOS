#include <stdint.h>
#include <stddef.h>
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/serial.h"
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/timer.h"
#include "kernel/02_Pengelolaan_Proses_Thread/process.h"
#include "kernel/03_Penjadwalan_CPU_Preemptive/scheduler.h"
#include "kernel/06_Alokator_Memori_Dinamis/heap.h"
#include "kernel/07_Virtual_File_System/vfs.h"
#include "shell/shell.h"

#define VGA_WIDTH        80
#define VGA_HEIGHT       25
#define SCROLLBACK_LINES 200

volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;
static volatile uint16_t history_buffer[SCROLLBACK_LINES][VGA_WIDTH];
int cursor_row = 0;
int cursor_col = 0;
static int scroll_offset = 0;

/* Counter aktivitas WorkerA dan WorkerB untuk perintah schedtest */
static volatile uint32_t worker_a_counter = 0;
static volatile uint32_t worker_b_counter = 0;

uint32_t kernel_get_worker_a_counter(void) {
    return worker_a_counter;
}

uint32_t kernel_get_worker_b_counter(void) {
    return worker_b_counter;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static int get_max_scroll_offset(void) {
    if (cursor_row >= VGA_HEIGHT) {
        return cursor_row - (VGA_HEIGHT - 1);
    }
    return 0;
}

void vga_update_cursor(void) {
    int base_row = get_max_scroll_offset();
    int view_start = base_row - scroll_offset;
    int screen_row = cursor_row - view_start;
    uint16_t pos;

    if (screen_row >= 0 && screen_row < VGA_HEIGHT) {
        pos = (uint16_t)(screen_row * VGA_WIDTH + cursor_col);
    } else {
        pos = (uint16_t)(VGA_WIDTH * VGA_HEIGHT);
    }

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_render(void) {
    int base_row = get_max_scroll_offset();
    int view_start = base_row - scroll_offset;

    for (int row = 0; row < VGA_HEIGHT; row++) {
        int hist_row = view_start + row;
        for (int col = 0; col < VGA_WIDTH; col++) {
            if (hist_row >= 0 && hist_row < SCROLLBACK_LINES) {
                vga_buffer[row * VGA_WIDTH + col] = history_buffer[hist_row][col];
            } else {
                vga_buffer[row * VGA_WIDTH + col] = ((uint16_t)0x07 << 8) | ' ';
            }
        }
    }
    vga_update_cursor();
}

void vga_scroll_view(int delta_lines) {
    int max_offset = get_max_scroll_offset();
    scroll_offset += delta_lines;

    if (scroll_offset < 0) {
        scroll_offset = 0;
    } else if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }

    vga_render();
}

void vga_scroll(uint8_t color) {
    if (cursor_row < SCROLLBACK_LINES) {
        return;
    }

    for (int row = 1; row < SCROLLBACK_LINES; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            history_buffer[row - 1][col] = history_buffer[row][col];
        }
    }

    for (int col = 0; col < VGA_WIDTH; col++) {
        history_buffer[SCROLLBACK_LINES - 1][col] = ((uint16_t)color << 8) | ' ';
    }

    cursor_row = SCROLLBACK_LINES - 1;
}

void vga_clear(uint8_t color) {
    for (int row = 0; row < SCROLLBACK_LINES; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            history_buffer[row][col] = ((uint16_t)color << 8) | ' ';
        }
    }

    cursor_row = 0;
    cursor_col = 0;
    scroll_offset = 0;
    vga_render();
}

void vga_putchar(char c, uint8_t color) {
    scroll_offset = 0;

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        serial_putchar('\n');
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
        }
        history_buffer[cursor_row][cursor_col] = ((uint16_t)color << 8) | ' ';
        serial_putchar('\b');
        serial_putchar(' ');
        serial_putchar('\b');
    } else if (c == '\t') {
        int spaces = 4 - (cursor_col % 4);
        for (int i = 0; i < spaces; i++) {
            vga_putchar(' ', color);
        }
        return;
    } else {
        history_buffer[cursor_row][cursor_col] = ((uint16_t)color << 8) | (uint8_t)c;
        cursor_col++;

        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }

        if (c >= ' ' && c <= '~') {
            serial_putchar(c);
        }
    }

    vga_scroll(0x07);
    vga_render();
}

void vga_print(const char* str, uint8_t color) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        uint8_t ch = (uint8_t)str[i];

        if (ch == 0xE2 && (uint8_t)str[i + 1] != '\0' && (uint8_t)str[i + 2] != '\0') {
            uint8_t b1 = (uint8_t)str[i + 1];
            uint8_t b2 = (uint8_t)str[i + 2];

            if (b1 == 0x96 && b2 == 0x88) {
                ch = 0xDB;
                i += 2;
            } else if (b1 == 0x95) {
                if (b2 == 0x90)      ch = 0xCD;
                else if (b2 == 0x91) ch = 0xBA;
                else if (b2 == 0x94) ch = 0xC9;
                else if (b2 == 0x97) ch = 0xBB;
                else if (b2 == 0x9A) ch = 0xC8;
                else if (b2 == 0x9D) ch = 0xBC;
                else if (b2 == 0xA0) ch = 0xCC;
                else if (b2 == 0xA3) ch = 0xB9;
                i += 2;
            } else if (b1 == 0x94 && b2 == 0x84) {
                ch = 0xC4;
                i += 2;
            }
        }

        vga_putchar((char)ch, color);
    }
}

void vga_print_number(size_t number) {
    char buffer[20];
    int i = 0;

    if (number == 0) {
        vga_putchar('0', 0x07);
        return;
    }

    while (number > 0) {
        buffer[i++] = (char)('0' + (number % 10));
        number /= 10;
    }

    while (i > 0) {
        vga_putchar(buffer[--i], 0x07);
    }
}

void print_banner(void) {
    vga_print("=====================================================================================\n", 0x0B);
    vga_print("     /\\          ██████╗  ██████╗ ███████╗\n", 0x0B);
    vga_print("    /  \\        ██╔════╝ ██╔═══██╗██╔════╝\n", 0x0B);
    vga_print("   / /\\ \\       ██║  ███╗██║   ██║███████╗\n", 0x0B);
    vga_print("  / /  \\ \\      ██║   ██║██║   ██║╚════██║\n", 0x0B);
    vga_print(" /_/__  \\_\\     ╚██████╔╝╚██████╔╝███████║\n", 0x0B);
    vga_print("   \\_____/       ╚═════╝  ╚═════╝ ╚══════╝\n", 0x0B);
    vga_print("=====================================================================================\n", 0x0B);
}

void system_shutdown(void) {
    vga_print("[SYSTEM] Shutting down GibranOS...\n", 0x0E);

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    vga_print("[SYSTEM] System halted.\n", 0x0C);
    __asm__ volatile ("cli");

    while (1) {
        __asm__ volatile ("hlt");
    }
}

/* Thread Worker A */
static void worker_a_task(void) {
    while (1) {
        worker_a_counter++;
    }
}

/* Thread Worker B */
static void worker_b_task(void) {
    while (1) {
        worker_b_counter++;
    }
}

void kernel_main(void) {
    __asm__ volatile ("cli");

    vga_clear(0x07);

    gdt_init();
    idt_init();
    timer_init();

    heap_init();

    /* Inisialisasi scheduler dan proses */
    scheduler_init();
    process_init();

    /* Membuat dua worker dan satu thread CLI Shell */
    pcb_t* worker_a = process_create(
        "WorkerA", worker_a_task, 1, PRIVILEGE_KERNEL
    );

    pcb_t* worker_b = process_create(
        "WorkerB", worker_b_task, 1, PRIVILEGE_KERNEL
    );

    pcb_t* shell = process_create(
        "Shell", shell_task, 1, PRIVILEGE_KERNEL
    );

    keyboard_init();
    mouse_init();
    vfs_init();
    serial_init();

    print_banner();

    vga_print("[BOOT] GibranOS Kernel Initialized.\n", 0x0A);
    vga_print("[VFS]  RAMDisk Initialized (words.txt, scores.txt loaded).\n", 0x0A);
    vga_print("[MEM]  Kernel Heap Allocator Ready (64 KB, first-fit).\n", 0x0A);
    vga_print("[PROC] Process Management Ready.\n", 0x0A);
    vga_print("[SCHED] Round Robin Scheduler Ready (Quantum 5).\n", 0x0A);

    vga_print(
        worker_a != NULL && worker_b != NULL && shell != NULL
        ? "[THREAD] WorkerA, WorkerB, Shell berhasil dibuat.\n"
        : "[ERROR] Gagal membuat salah satu thread.\n",
        worker_a != NULL && worker_b != NULL && shell != NULL
        ? 0x0A : 0x0C
    );

    vga_print(
        serial_is_ready()
        ? "[IO] Serial UART COM1 Ready (38400 8N1).\n"
        : "[IO] Serial UART COM1 tidak terdeteksi.\n",
        0x0A
    );

    if (worker_a == NULL || worker_b == NULL || shell == NULL) {
        vga_print("[ERROR] Scheduler tidak diaktifkan.\n", 0x0C);

        while (1) {
            __asm__ volatile ("hlt");
        }
    }

    /* Aktifkan interrupt setelah semua thread siap */
    __asm__ volatile ("sti");

    /* Kernel utama menjadi idle */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
