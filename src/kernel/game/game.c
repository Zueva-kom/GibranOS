#include "kernel/game/game.h"
#include "drivers/keyboard.h"
#include "kernel/fs/vfs.h"

#define ARENA_WIDTH  80
#define ARENA_HEIGHT 25
#define MAX_WORDS    32
#define WORD_MAX_LEN 24

/* Kembalikan tampilan CLI saat keluar dari arena game */
extern void vga_render(void);
extern void vga_print(const char* str, uint8_t color);

static volatile uint16_t* const vga_mem = (volatile uint16_t*)0xB8000;

/* Buffer belakang (backbuffer) untuk mencegah efek berkedip (flicker) saat render */
static volatile uint16_t arena_buf[ARENA_HEIGHT][ARENA_WIDTH];

/* Karakter box-drawing CP437 */
#define BOX_H   ((char)0xCD) /* ═ */
#define BOX_V   ((char)0xBA) /* ║ */
#define BOX_TL  ((char)0xC9) /* ╔ */
#define BOX_TR  ((char)0xBB) /* ╗ */
#define BOX_BL  ((char)0xC8) /* ╚ */
#define BOX_BR  ((char)0xBC) /* ╝ */
#define BOX_ML  ((char)0xCC) /* ╠ */
#define BOX_MR  ((char)0xB9) /* ╣ */
#define BOX_TM  ((char)0xCB) /* ╦ */
#define BOX_BM  ((char)0xCA) /* ╩ */
#define BOX_SH  ((char)0xC4) /* ─ */
#define SYM_BLK ((char)0xDB) /* █ */
#define SYM_SHD ((char)0xB0) /* ░ */
#define SYM_HRT ((char)0x03) /* ♥ */
#define SYM_UP  ((char)0x1E) /* ▲ */

static inline void game_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Sembunyikan kursor perangkat keras VGA selama berada di UI Arena */
static void hide_vga_cursor(void) {
    uint16_t pos = ARENA_WIDTH * ARENA_HEIGHT;
    game_outb(0x3D4, 0x0F);
    game_outb(0x3D5, (uint8_t)(pos & 0xFF));
    game_outb(0x3D4, 0x0E);
    game_outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* Bersihkan backbuffer arena */
static void arena_clear(uint8_t color) {
    uint16_t blank = ((uint16_t)color << 8) | ' ';
    for (int r = 0; r < ARENA_HEIGHT; r++) {
        for (int c = 0; c < ARENA_WIDTH; c++) {
            arena_buf[r][c] = blank;
        }
    }
}

/* Gambar 1 karakter ke koordinat (row, col) pada backbuffer */
static void arena_putc(int row, int col, char ch, uint8_t color) {
    if (row >= 0 && row < ARENA_HEIGHT && col >= 0 && col < ARENA_WIDTH) {
        arena_buf[row][col] = ((uint16_t)color << 8) | (uint8_t)ch;
    }
}

/* Gambar string ASCII ke koordinat (row, col) pada backbuffer */
static void arena_puts(int row, int col, const char* str, uint8_t color) {
    int c = col;
    while (*str != '\0' && c < ARENA_WIDTH) {
        arena_putc(row, c++, *str++, color);
    }
}

/* Gambar angka integer ke koordinat (row, col) */
static int arena_put_uint(int row, int col, uint32_t val, uint8_t color) {
    char tmp[12];
    int i = 0;

    if (val == 0) {
        arena_putc(row, col, '0', color);
        return 1;
    }
    while (val > 0 && i < 11) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int len = i;
    while (i > 0) {
        arena_putc(row, col++, tmp[--i], color);
    }
    return len;
}

/* Salin seluruh isi backbuffer ke memori layar VGA (0xB8000) */
static void arena_present(void) {
    for (int r = 0; r < ARENA_HEIGHT; r++) {
        for (int c = 0; c < ARENA_WIDTH; c++) {
            vga_mem[r * ARENA_WIDTH + c] = arena_buf[r][c];
        }
    }
}

static size_t game_strlen(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

static int game_streq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static uint32_t str_to_uint(const char* s) {
    uint32_t val = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return val;
}

static void uint_to_str(uint32_t val, char* out) {
    char tmp[12];
    int i = 0, j = 0;
    if (val == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (val > 0 && i < 11) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

/* Memecah isi words.txt dari VFS menjadi daftar kata untuk soal typing */
static int load_words_from_vfs(char word_list[MAX_WORDS][WORD_MAX_LEN]) {
    char raw[MAX_FILESIZE];
    int count = 0;

    if (vfs_read("words.txt", raw) != VFS_OK) {
        return 0;
    }

    size_t i = 0;
    while (raw[i] != '\0' && count < MAX_WORDS) {
        while (raw[i] == ' ' || raw[i] == '\n' || raw[i] == '\r' || raw[i] == '\t') {
            i++;
        }
        if (raw[i] == '\0') break;

        size_t w = 0;
        while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\n' &&
               raw[i] != '\r' && raw[i] != '\t' && w < WORD_MAX_LEN - 1) {
            word_list[count][w++] = raw[i++];
        }
        word_list[count][w] = '\0';
        if (w > 0) {
            count++;
        }
    }
    return count;
}

/*
 * Menggambar seluruh elemen UI Arena:
 * - Header HUD (Skor, Rekor VFS, Waktu, HP Pahlawan)
 * - Panel Kiri (Cols 0..51): Arena Turunnya Monster & Pahlawan Penembak Laser
 * - Panel Kanan (Cols 52..79): Radar Jarak Monster & Status Pertahanan
 * - Panel Bawah (Rows 19..24): Kata Target & Kotak Ketikan Pemain (Input Box)
 */
static void render_arena_frame(
    const char* target_word,
    const char* typed_buf,
    uint32_t score,
    uint32_t high_score,
    uint32_t time_left,
    int hp,
    int monster_y,       /* 0 (paling atas) s/d 8 (menyentuh garis pahlawan) */
    int laser_frames,    /* > 0 saat pahlawan sedang menembakkan laser ke atas */
    const char* status_msg,
    uint8_t status_color
) {
    arena_clear(0x07);

    /* 1. Bingkai Utama 80x25 */
    for (int c = 1; c < ARENA_WIDTH - 1; c++) {
        arena_putc(0, c, BOX_H, 0x0B);
        arena_putc(2, c, BOX_H, 0x0B);
        arena_putc(19, c, BOX_H, 0x0B);
        arena_putc(22, c, BOX_H, 0x0B);
        arena_putc(24, c, BOX_H, 0x0B);
    }
    for (int r = 1; r < ARENA_HEIGHT - 1; r++) {
        arena_putc(r, 0, BOX_V, 0x0B);
        arena_putc(r, ARENA_WIDTH - 1, BOX_V, 0x0B);
        if (r >= 3 && r <= 18) {
            arena_putc(r, 51, BOX_V, 0x0B);
        }
    }

    /* Sudut dan persimpangan bingkai */
    arena_putc(0, 0, BOX_TL, 0x0B);
    arena_putc(0, ARENA_WIDTH - 1, BOX_TR, 0x0B);
    arena_putc(2, 0, BOX_ML, 0x0B);
    arena_putc(2, 51, BOX_TM, 0x0B);
    arena_putc(2, ARENA_WIDTH - 1, BOX_MR, 0x0B);
    arena_putc(19, 0, BOX_ML, 0x0B);
    arena_putc(19, 51, BOX_BM, 0x0B);
    arena_putc(19, ARENA_WIDTH - 1, BOX_MR, 0x0B);
    arena_putc(22, 0, BOX_ML, 0x0B);
    arena_putc(22, ARENA_WIDTH - 1, BOX_MR, 0x0B);
    arena_putc(24, 0, BOX_BL, 0x0B);
    arena_putc(24, ARENA_WIDTH - 1, BOX_BR, 0x0B);

    /* 2. Baris HUD Atas (Row 1) */
    arena_puts(1, 2, "GIBRANOS MONSTER TYPING", 0x0E);
    arena_puts(1, 28, "SKOR: ", 0x07);
    arena_put_uint(1, 34, score, 0x0A);

    arena_puts(1, 42, "REKOR: ", 0x07);
    arena_put_uint(1, 49, high_score, 0x0B);

    arena_puts(1, 57, "WAKTU: ", 0x07);
    int tlen = arena_put_uint(1, 64, time_left, (time_left <= 10) ? 0x0C : 0x0E);
    arena_putc(1, 64 + tlen, 's', 0x07);

    arena_puts(1, 70, "HP: ", 0x07);
    for (int i = 0; i < 3; i++) {
        arena_putc(1, 74 + i, SYM_HRT, (i < hp) ? 0x0C : 0x08);
    }

    /* 3. Area Pertempuran Kiri (Cols 1..50, Rows 3..18) */
    arena_puts(3, 6, "~ ~ [ PORTAL LANGIT: INVASI MONSTER ] ~ ~", 0x0D);

    /* Garis pertahanan pahlawan di bawah (Row 15) */
    for (int c = 2; c < 50; c++) {
        arena_putc(15, c, BOX_SH, 0x08);
    }
    arena_puts(15, 17, "[ GARIS PERTAHANAN ]", 0x08);

    /* Posisi vertikal monster: baris 4 s/d 12 (3 baris tinggi sprite) */
    int m_row = 4 + monster_y;
    if (m_row > 12) m_row = 12;

    /* Jika pahlawan baru saja menyelesaikan kata, gambar tembakan laser ke atas! */
    if (laser_frames > 0) {
        for (int r = m_row + 3; r <= 15; r++) {
            arena_putc(r, 25, SYM_UP, 0x0B);
            arena_putc(r, 26, BOX_V, 0x0E);
            arena_putc(r, 27, SYM_UP, 0x0B);
        }
        arena_puts(m_row + 2, 19, "** KENA! MUNDUR! **", 0x0E);
    }

    /* Gambar Sprite Monster yang turun dari atas */
    uint8_t m_color = (monster_y >= 6) ? 0x0C : ((monster_y >= 3) ? 0x0E : 0x0A);
    arena_puts(m_row,     19, "  /\\___/\\  ", m_color);
    arena_puts(m_row + 1, 19, " ( @ w @ ) ", m_color);
    arena_puts(m_row + 2, 19, "  /|   |\\  ", m_color);

    /* Label kata target yang melekat di sebelah monster */
    arena_puts(m_row + 1, 32, "<", 0x0F);
    arena_puts(m_row + 1, 33, target_word, 0x0E);
    arena_puts(m_row + 1, 33 + (int)game_strlen(target_word), ">", 0x0F);

    /* Gambar Sprite Pahlawan & Meriam Energi di bawah (Rows 16..18) */
    uint8_t hero_color = (laser_frames > 0) ? 0x0E : 0x0B;
    arena_putc(16, 25, '[', 0x0F);
    arena_putc(16, 26, SYM_UP, hero_color);
    arena_putc(16, 27, ']', 0x0F);
    arena_puts(17, 21, "   \\(O)/   ", 0x0A);
    arena_puts(18, 17, "===[ BENTENG HERO ]===", 0x09);

    /* 4. Panel Informasi & Radar di Kanan (Cols 52..78, Rows 3..18) */
    arena_puts(3, 53, "RADAR JARAK MONSTER:", 0x0E);
    arena_putc(4, 53, '[', 0x0F);
    for (int i = 0; i < 16; i++) {
        int threshold = ((monster_y + 1) * 16) / 9;
        if (i < threshold) {
            uint8_t bar_col = (monster_y >= 6) ? 0x0C : 0x0E;
            arena_putc(4, 54 + i, SYM_BLK, bar_col);
        } else {
            arena_putc(4, 54 + i, SYM_SHD, 0x08);
        }
    }
    arena_putc(4, 70, ']', 0x0F);

    arena_puts(5, 53, "Status: ", 0x07);
    if (monster_y >= 6) {
        arena_puts(5, 61, "BAHAYA KRITIS!", 0x0C);
    } else if (monster_y >= 3) {
        arena_puts(5, 61, "WASPADA!      ", 0x0E);
    } else {
        arena_puts(5, 61, "AMAN          ", 0x0A);
    }

    for (int c = 52; c < ARENA_WIDTH - 1; c++) {
        arena_putc(6, c, BOX_SH, 0x08);
        arena_putc(14, c, BOX_SH, 0x08);
    }

    arena_puts(7,  53, "CARA BERMAIN:", 0x0B);
    arena_puts(8,  53, "1. Monster turun dari", 0x07);
    arena_puts(9,  53, "   portal langit.", 0x07);
    arena_puts(10, 53, "2. Ketik KATA TARGET", 0x07);
    arena_puts(11, 53, "   hingga lengkap.", 0x07);
    arena_puts(12, 53, "3. Hero akan menembak", 0x07);
    arena_puts(13, 53, "   & mendorong balik!", 0x07);

    arena_puts(15, 53, "LOG PERTEMPURAN:", 0x0B);
    arena_puts(16, 53, status_msg, status_color);
    arena_puts(18, 53, "[ESC] Kembali ke CLI", 0x08);

    /* 5. Panel Bawah: Kata Target & Input Ketikan Pemain (Rows 20..23) */
    arena_puts(20, 3, "KATA TARGET  : [ ", 0x0B);
    size_t t_len = game_strlen(target_word);
    size_t u_len = game_strlen(typed_buf);
    int prefix_ok = 1;

    for (size_t i = 0; i < t_len; i++) {
        uint8_t ch_color = 0x0F;
        if (i < u_len) {
            if (prefix_ok && typed_buf[i] == target_word[i]) {
                ch_color = 0x0A; /* Hijau jika huruf sesuai */
            } else {
                prefix_ok = 0;
                ch_color = 0x0C; /* Merah jika salah ketik */
            }
        }
        arena_putc(20, 20 + (int)(i * 2), target_word[i], ch_color);
    }
    arena_puts(20, 20 + (int)(t_len * 2), " ]", 0x0B);

    arena_puts(21, 3, "INPUT PEMAIN : > ", 0x0E);
    uint8_t input_color = prefix_ok ? 0x0A : 0x0C;
    arena_puts(21, 20, typed_buf, input_color);
    arena_putc(21, 20 + (int)u_len, '_', 0x0F);

    arena_puts(23, 3, "Ketik kata target dengan cepat untuk menembak monster!", 0x07);
    arena_puts(23, 60, "[ESC] Keluar Game", 0x0E);

    arena_present();
}

void game_start(void) {
    char word_list[MAX_WORDS][WORD_MAX_LEN];
    char score_str[MAX_FILESIZE];
    char typed_buf[WORD_MAX_LEN];
    size_t typed_len = 0;

    int word_count = load_words_from_vfs(word_list);
    if (word_count <= 0) {
        /* Fallback jika words.txt kosong */
        const char* defaults[] = {"kernel", "proses", "thread", "memori", "mutex", "gibranos"};
        for (int i = 0; i < 6; i++) {
            size_t j = 0;
            while (defaults[i][j] != '\0') {
                word_list[i][j] = defaults[i][j];
                j++;
            }
            word_list[i][j] = '\0';
        }
        word_count = 6;
    }

    uint32_t high_score = 0;
    if (vfs_read("scores.txt", score_str) == VFS_OK) {
        high_score = str_to_uint(score_str);
    }

    uint32_t score = 0;
    uint32_t time_left = 60;
    int hp = 3;
    int monster_y = 0;
    int current_word_idx = 0;
    int laser_frames = 0;
    const char* status_msg = "> Monster mendekat!";
    uint8_t status_color = 0x0E;

    typed_buf[0] = '\0';
    hide_vga_cursor();

    render_arena_frame(
        word_list[current_word_idx],
        typed_buf,
        score,
        high_score,
        time_left,
        hp,
        monster_y,
        laser_frames,
        status_msg,
        status_color
    );

    uint32_t tick_counter = 0;
    uint32_t monster_tick = 0;
    uint32_t second_tick  = 0;

    while (1) {
        char c = keyboard_poll_char();

        if (c == 27) { /* ESC: keluar kembali ke CLI */
            break;
        }

        if (hp > 0 && time_left > 0) {
            if (c == '\b') {
                if (typed_len > 0) {
                    typed_len--;
                    typed_buf[typed_len] = '\0';
                }
            } else if (c >= ' ' && c <= '~' && c != ' ') {
                if (typed_len < WORD_MAX_LEN - 1) {
                    typed_buf[typed_len++] = c;
                    typed_buf[typed_len] = '\0';
                }

                /* Cek apakah kata target sudah berhasil diketik penuh & tepat */
                if (game_streq(typed_buf, word_list[current_word_idx])) {
                    score += 10;
                    if (score > high_score) {
                        high_score = score;
                        char new_score_str[16];
                        uint_to_str(high_score, new_score_str);
                        vfs_kernel_update("scores.txt", new_score_str);
                    }

                    /* Dorong monster naik kembali dengan tembakan laser pahlawan! */
                    if (monster_y >= 2) {
                        monster_y -= 2;
                    } else {
                        monster_y = 0;
                    }
                    monster_tick = 0;
                    laser_frames = 18;
                    status_msg = "> TEMBAKAN TELAK! +10";
                    status_color = 0x0A;

                    /* Ganti ke kata target berikutnya */
                    current_word_idx = (current_word_idx + 1) % word_count;
                    typed_len = 0;
                    typed_buf[0] = '\0';
                }
            }

            /* Simulasi detak waktu & pergerakan turun monster */
            for (volatile int delay = 0; delay < 35000; delay++) {
                __asm__ volatile ("nop");
            }
            tick_counter++;
            monster_tick++;
            second_tick++;

            if (laser_frames > 0) {
                laser_frames--;
            }

            /* Monster turun 1 tingkat secara berkala */
            if (monster_tick >= 95) {
                monster_tick = 0;
                monster_y++;

                if (monster_y > 8) {
                    /* Monster mencapai benteng pahlawan! */
                    hp--;
                    monster_y = 1;
                    typed_len = 0;
                    typed_buf[0] = '\0';
                    current_word_idx = (current_word_idx + 1) % word_count;
                    status_msg = "> BENTENG TERTABRAK!";
                    status_color = 0x0C;
                } else if (laser_frames == 0) {
                    status_msg = "> Monster turun...";
                    status_color = (monster_y >= 6) ? 0x0C : 0x0E;
                }
            }

            /* Hitung mundur waktu permainan (detik) */
            if (second_tick >= 80) {
                second_tick = 0;
                if (time_left > 0) {
                    time_left--;
                }
            }

            if (hp <= 0) {
                status_msg = "GAME OVER! [ENTER/ESC]";
                status_color = 0x0C;
            } else if (time_left == 0) {
                status_msg = "MENANG! [ENTER/ESC]";
                status_color = 0x0A;
            }

            if (c != 0 || (tick_counter % 8 == 0)) {
                render_arena_frame(
                    word_list[current_word_idx],
                    typed_buf,
                    score,
                    high_score,
                    time_left,
                    hp,
                    monster_y,
                    laser_frames,
                    status_msg,
                    status_color
                );
            }
        } else {
            /* Kondisi Game Selesai (Menang / Kalah): tekan ENTER untuk ulang atau ESC untuk keluar */
            if (c == '\n') {
                score = 0;
                time_left = 60;
                hp = 3;
                monster_y = 0;
                typed_len = 0;
                typed_buf[0] = '\0';
                current_word_idx = (current_word_idx + 1) % word_count;
                status_msg = "> Ronde baru dimulai!";
                status_color = 0x0A;
                render_arena_frame(
                    word_list[current_word_idx],
                    typed_buf,
                    score,
                    high_score,
                    time_left,
                    hp,
                    monster_y,
                    0,
                    status_msg,
                    status_color
                );
            }
        }
    }

    /* Pulihkan tampilan layar CLI GibranOS */
    vga_render();
    vga_print("[GAME] Sesi permainan selesai. Kembali ke GibranOS-CLI.\n", 0x0A);
}
