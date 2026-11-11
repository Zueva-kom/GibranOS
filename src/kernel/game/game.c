#include "kernel/game/game.h"
#include "drivers/keyboard.h"
#include "kernel/07_Virtual_File_System/vfs.h"

#define ARENA_WIDTH   80
#define ARENA_HEIGHT  25
#define MAX_WORD_POOL 160
#define WORD_MAX_LEN  20
#define NAME_MAX_LEN  16

extern void vga_render(void);
extern void vga_print(const char* str, uint8_t color);

static volatile uint16_t* const vga_mem = (volatile uint16_t*)0xB8000;
static volatile uint16_t arena_buf[ARENA_HEIGHT][ARENA_WIDTH];

/* Buffer statis agar tidak membebani stack kernel */
static char vfs_raw_words[MAX_FILESIZE];
static char vfs_score_record[MAX_FILESIZE];
static char word_pool[MAX_WORD_POOL][WORD_MAX_LEN];

/* Karakter CP437 untuk UI minimalis */
#define LINE_H  ((char)0xC4) /* ─ */
#define LINE_V  ((char)0xB3) /* │ */
#define BOX_TL  ((char)0xDA) /* ┌ */
#define BOX_TR  ((char)0xBF) /* ┐ */
#define BOX_BL  ((char)0xC0) /* └ */
#define BOX_BR  ((char)0xD9) /* ┘ */
#define SYM_HRT ((char)0x03) /* ♥ */
#define SYM_UP  ((char)0x1E) /* ▲ */

typedef enum {
    DIFF_EASY = 0,
    DIFF_MEDIUM = 1,
    DIFF_HARD = 2
} difficulty_t;

static inline void game_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t game_inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static uint8_t rtc_read_second(void) {
    game_outb(0x70, 0x0A);
    if (game_inb(0x71) & 0x80) {
        return 0xFF;
    }
    game_outb(0x70, 0x00);
    return game_inb(0x71);
}

static void hide_vga_cursor(void) {
    uint16_t pos = ARENA_WIDTH * ARENA_HEIGHT;
    game_outb(0x3D4, 0x0F);
    game_outb(0x3D5, (uint8_t)(pos & 0xFF));
    game_outb(0x3D4, 0x0E);
    game_outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void arena_clear(uint8_t color) {
    uint16_t blank = ((uint16_t)color << 8) | ' ';
    for (int r = 0; r < ARENA_HEIGHT; r++) {
        for (int c = 0; c < ARENA_WIDTH; c++) {
            arena_buf[r][c] = blank;
        }
    }
}

static void arena_putc(int row, int col, char ch, uint8_t color) {
    if (row >= 0 && row < ARENA_HEIGHT && col >= 0 && col < ARENA_WIDTH) {
        arena_buf[row][col] = ((uint16_t)color << 8) | (uint8_t)ch;
    }
}

static void arena_puts(int row, int col, const char* str, uint8_t color) {
    int c = col;
    while (*str != '\0' && c < ARENA_WIDTH) {
        arena_putc(row, c++, *str++, color);
    }
}

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

static void arena_hline(int row, int c1, int c2, uint8_t color) {
    for (int c = c1; c <= c2; c++) {
        arena_putc(row, c, LINE_H, color);
    }
}

static void arena_box(int r1, int c1, int r2, int c2, uint8_t color) {
    arena_hline(r1, c1 + 1, c2 - 1, color);
    arena_hline(r2, c1 + 1, c2 - 1, color);
    for (int r = r1 + 1; r < r2; r++) {
        arena_putc(r, c1, LINE_V, color);
        arena_putc(r, c2, LINE_V, color);
    }
    arena_putc(r1, c1, BOX_TL, color);
    arena_putc(r1, c2, BOX_TR, color);
    arena_putc(r2, c1, BOX_BL, color);
    arena_putc(r2, c2, BOX_BR, color);
}

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

static const char* diff_to_name(difficulty_t diff) {
    if (diff == DIFF_EASY)   return "MUDAH";
    if (diff == DIFF_MEDIUM) return "SEDANG";
    return "SULIT";
}

/* Format penyimpanan ke scores.txt: "<skor> - <nama> (<level>)" */
static void save_highscore_to_vfs(uint32_t score, const char* player_name, difficulty_t diff) {
    char buf[96];
    char num[12];
    int n = 0, idx = 0;

    uint32_t tmp = score;
    if (tmp == 0) {
        num[n++] = '0';
    } else {
        while (tmp > 0 && n < 11) {
            num[n++] = (char)('0' + (tmp % 10));
            tmp /= 10;
        }
    }
    while (n > 0) {
        buf[idx++] = num[--n];
    }

    const char* sep = " - ";
    for (int i = 0; sep[i] != '\0'; i++) buf[idx++] = sep[i];

    for (int i = 0; player_name[i] != '\0' && idx < 70; i++) {
        buf[idx++] = player_name[i];
    }

    buf[idx++] = ' ';
    buf[idx++] = '(';
    const char* dname = diff_to_name(diff);
    for (int i = 0; dname[i] != '\0'; i++) buf[idx++] = dname[i];
    buf[idx++] = ')';
    buf[idx] = '\0';

    vfs_kernel_update("scores.txt", buf);
    for (int i = 0; i <= idx; i++) {
        vfs_score_record[i] = buf[i];
    }
}

/* Memuat kata dari words.txt di VFS sesuai tingkat kesulitan */
static int load_words_for_difficulty(difficulty_t diff) {
    int count = 0;

    if (vfs_read("words.txt", vfs_raw_words) != VFS_OK) {
        return 0;
    }

    size_t min_len = 1, max_len = 30;
    if (diff == DIFF_EASY) {
        min_len = 3;
        max_len = 5;
    } else if (diff == DIFF_MEDIUM) {
        min_len = 6;
        max_len = 8;
    } else {
        min_len = 9;
        max_len = 20;
    }

    size_t i = 0;
    while (vfs_raw_words[i] != '\0' && count < MAX_WORD_POOL) {
        while (vfs_raw_words[i] == ' ' || vfs_raw_words[i] == '\n' ||
               vfs_raw_words[i] == '\r' || vfs_raw_words[i] == '\t') {
            i++;
        }
        if (vfs_raw_words[i] == '\0') break;

        char temp[WORD_MAX_LEN];
        size_t w = 0;
        while (vfs_raw_words[i] != '\0' && vfs_raw_words[i] != ' ' &&
               vfs_raw_words[i] != '\n' && vfs_raw_words[i] != '\r' &&
               vfs_raw_words[i] != '\t') {
            if (w < WORD_MAX_LEN - 1) {
                temp[w++] = vfs_raw_words[i];
            }
            i++;
        }
        temp[w] = '\0';

        if (w >= min_len && w <= max_len) {
            for (size_t k = 0; k <= w; k++) {
                word_pool[count][k] = temp[k];
            }
            count++;
        }
    }

    /* Jika pengguna mengubah words.txt di CLI dan tidak ada kata sepanjang level itu, ambil semua kata */
    if (count == 0) {
        i = 0;
        while (vfs_raw_words[i] != '\0' && count < MAX_WORD_POOL) {
            while (vfs_raw_words[i] == ' ' || vfs_raw_words[i] == '\n' ||
                   vfs_raw_words[i] == '\r' || vfs_raw_words[i] == '\t') {
                i++;
            }
            if (vfs_raw_words[i] == '\0') break;

            size_t w = 0;
            while (vfs_raw_words[i] != '\0' && vfs_raw_words[i] != ' ' &&
                   vfs_raw_words[i] != '\n' && vfs_raw_words[i] != '\r' &&
                   vfs_raw_words[i] != '\t' && w < WORD_MAX_LEN - 1) {
                word_pool[count][w++] = vfs_raw_words[i++];
            }
            word_pool[count][w] = '\0';
            if (w > 0) count++;
        }
    }

    return count;
}

/* Render Menu Awal Minimalis: Input Nama Pemain & Pilihan Tingkat Kesulitan */
static void render_setup_menu(const char* player_name, difficulty_t diff, const char* high_record) {
    arena_clear(0x07);

    arena_box(3, 10, 21, 70, 0x08);

    arena_puts(5, 28, "GIBRANOS TYPING DEFENSE", 0x0B);
    arena_hline(6, 12, 68, 0x08);

    /* Input Nama User */
    arena_puts(8, 18, "Nama Pemain :", 0x07);
    arena_puts(8, 33, "[ ", 0x08);
    arena_puts(8, 35, player_name, 0x0F);
    arena_putc(8, 35 + (int)game_strlen(player_name), '_', 0x0A);
    arena_puts(8, 53, "]", 0x08);

    /* Pilihan Tingkat Kesulitan */
    arena_puts(11, 18, "Kesulitan   : (Gunakan Panah Atas/Bawah)", 0x07);

    arena_puts(13, 20, (diff == DIFF_EASY)   ? "> [MUDAH]  Kata 4-5 huruf  | Kecepatan: Lambat (4d/langkah)" : "  [MUDAH]  Kata 4-5 huruf  | Kecepatan: Lambat (4d/langkah)",
               (diff == DIFF_EASY)   ? 0x0A : 0x08);
    arena_puts(14, 20, (diff == DIFF_MEDIUM) ? "> [SEDANG] Kata 6-8 huruf  | Kecepatan: Sedang (2d/langkah)" : "  [SEDANG] Kata 6-8 huruf  | Kecepatan: Sedang (2d/langkah)",
               (diff == DIFF_MEDIUM) ? 0x0E : 0x08);
    arena_puts(15, 20, (diff == DIFF_HARD)   ? "> [SULIT]  Kata 9+ huruf   | Kecepatan: Cepat  (1d/langkah)" : "  [SULIT]  Kata 9+ huruf   | Kecepatan: Cepat  (1d/langkah)",
               (diff == DIFF_HARD)   ? 0x0C : 0x08);

    arena_hline(17, 12, 68, 0x08);
    arena_puts(18, 18, "Rekor (scores.txt) : ", 0x08);
    arena_puts(18, 39, high_record, 0x0B);

    arena_puts(20, 18, "[ENTER] Mulai Main", 0x0A);
    arena_puts(20, 46, "[ESC] Kembali CLI", 0x08);

    arena_present();
}

/* Render Arena Permainan Minimalis */
static void render_minimal_arena(
    const char* player_name,
    difficulty_t diff,
    const char* target_word,
    const char* typed_buf,
    uint32_t score,
    const char* high_record,
    uint32_t time_left,
    int hp,
    int monster_y,
    int laser_active,
    const char* status_msg,
    uint8_t status_color
) {
    arena_clear(0x07);

    /* 1. Header Bar Minimalis (Row 1) */
    arena_puts(1, 2, "USER: ", 0x08);
    arena_puts(1, 8, player_name, 0x0F);

    arena_puts(1, 24, "LEVEL: ", 0x08);
    uint8_t dcol = (diff == DIFF_EASY) ? 0x0A : ((diff == DIFF_MEDIUM) ? 0x0E : 0x0C);
    arena_puts(1, 31, diff_to_name(diff), dcol);

    arena_puts(1, 41, "SKOR: ", 0x08);
    arena_put_uint(1, 47, score, 0x0A);

    arena_puts(1, 56, "WAKTU: ", 0x08);
    int tlen = arena_put_uint(1, 63, time_left, (time_left <= 10) ? 0x0C : 0x0F);
    arena_putc(1, 63 + tlen, 's', 0x08);

    arena_puts(1, 70, "HP: ", 0x08);
    for (int i = 0; i < 3; i++) {
        arena_putc(1, 74 + i, SYM_HRT, (i < hp) ? 0x0C : 0x08);
    }

    arena_hline(2, 0, ARENA_WIDTH - 1, 0x08);

    /* 2. Area Tengah: Monster Turun dari Atas & Pahlawan di Bawah (Rows 3..17) */
    int m_row = 3 + monster_y; /* monster_y: 0 s/d 9 */
    if (m_row > 12) m_row = 12;

    /* Sinar laser vertikal minimalis saat kata selesai diketik */
    if (laser_active) {
        for (int r = m_row + 3; r <= 15; r++) {
            arena_putc(r, 39, LINE_V, 0x0B);
            arena_putc(r, 40, SYM_UP, 0x0E);
        }
    }

    /* Sprite Monster di tengah layar */
    uint8_t m_color = (monster_y >= 7) ? 0x0C : ((monster_y >= 4) ? 0x0E : 0x0A);
    arena_puts(m_row,     35, " /\\_/\\ ", m_color);
    arena_puts(m_row + 1, 35, "( o.o )", m_color);
    arena_puts(m_row + 2, 35, " > ^ < ", m_color);

    /* Garis Pertahanan & Pahlawan di bawah (Row 16-17) */
    arena_hline(16, 15, 64, 0x08);
    arena_puts(16, 37, "[ ", 0x08);
    arena_putc(16, 39, SYM_UP, laser_active ? 0x0E : 0x0B);
    arena_puts(16, 40, " ]", 0x08);
    arena_puts(17, 37, " \\O/ ", 0x0B);

    /* Pesan status singkat di kanan bawah arena */
    arena_puts(17, 54, status_msg, status_color);

    arena_hline(18, 0, ARENA_WIDTH - 1, 0x08);

    /* 3. Area Ketikan Minimalis di Bawah (Rows 20..24) */
    arena_puts(20, 18, "KATA TARGET  :  ", 0x07);
    size_t t_len = game_strlen(target_word);
    size_t u_len = game_strlen(typed_buf);
    int prefix_ok = 1;

    for (size_t i = 0; i < t_len; i++) {
        uint8_t ch_color = 0x0F;
        if (i < u_len) {
            if (prefix_ok && typed_buf[i] == target_word[i]) {
                ch_color = 0x0A; /* Hijau = benar */
            } else {
                prefix_ok = 0;
                ch_color = 0x0C; /* Merah = salah */
            }
        }
        arena_putc(20, 34 + (int)(i * 2), target_word[i], ch_color);
    }

    arena_puts(22, 18, "KETIKAN ANDA :  > ", 0x08);
    arena_puts(22, 36, typed_buf, prefix_ok ? 0x0A : 0x0C);
    arena_putc(22, 36 + (int)u_len, '_', 0x0F);

    arena_puts(24, 2, "[ESC] Menu / Keluar", 0x08);
    arena_puts(24, 38, "Rekor VFS: ", 0x08);
    arena_puts(24, 49, high_record, 0x07);

    arena_present();
}

void game_start(void) {
    char player_name[NAME_MAX_LEN] = "Player";
    size_t name_len = 6;
    difficulty_t diff = DIFF_EASY;

    if (vfs_read("scores.txt", vfs_score_record) != VFS_OK || vfs_score_record[0] == '\0') {
        vfs_score_record[0] = '0';
        vfs_score_record[1] = '\0';
    }
    uint32_t high_score = str_to_uint(vfs_score_record);

    hide_vga_cursor();

    /* Loop Menu Persiapan (Input Nama Pemain & Pilih Kesulitan) */
    while (1) {
        render_setup_menu(player_name, diff, vfs_score_record);

        int start_game = 0;
        while (1) {
            char c = keyboard_poll_char();
            if (c == 0) {
                __asm__ volatile ("pause");
                continue;
            }

            if (c == 27) { /* ESC dari menu utama -> kembali ke CLI */
                vga_render();
                vga_print("[GAME] Keluar ke GibranOS-CLI.\n", 0x0A);
                return;
            } else if (c == KEY_UP) {
                if (diff > DIFF_EASY) diff = (difficulty_t)(diff - 1);
                render_setup_menu(player_name, diff, vfs_score_record);
            } else if (c == KEY_DOWN || c == '\t') {
                if (diff < DIFF_HARD) diff = (difficulty_t)(diff + 1);
                else if (c == '\t') diff = DIFF_EASY;
                render_setup_menu(player_name, diff, vfs_score_record);
            } else if (c == '\b') {
                if (name_len > 0) {
                    name_len--;
                    player_name[name_len] = '\0';
                    render_setup_menu(player_name, diff, vfs_score_record);
                }
            } else if (c == '\n') {
                if (name_len == 0) {
                    const char* def = "Player";
                    for (int i = 0; def[i] != '\0'; i++) player_name[i] = def[i];
                    player_name[6] = '\0';
                    name_len = 6;
                }
                start_game = 1;
                break;
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_') {
                if (name_len < NAME_MAX_LEN - 1) {
                    player_name[name_len++] = c;
                    player_name[name_len] = '\0';
                    render_setup_menu(player_name, diff, vfs_score_record);
                }
            }
        }

        if (!start_game) break;

        /* Muat daftar kata dari words.txt sesuai tingkat kesulitan */
        int word_count = load_words_for_difficulty(diff);
        if (word_count <= 0) {
            const char* fallback = "kernel";
            for (int i = 0; i < 7; i++) word_pool[0][i] = fallback[i];
            word_count = 1;
        }

        /*
         * Parameter permainan berdasarkan tingkat kesulitan:
         * - MUDAH  : Monster turun tiap 4 detik (lambat), dorong mundur 3 langkah
         * - SEDANG : Monster turun tiap 2 detik (2x lebih cepat), dorong mundur 2 langkah
         * - SULIT  : Monster turun tiap 1 detik (4x lebih cepat), dorong mundur 2 langkah
         */
        uint32_t step_seconds = (diff == DIFF_EASY) ? 4 : ((diff == DIFF_MEDIUM) ? 2 : 1);
        int pushback_steps    = (diff == DIFF_EASY) ? 3 : 2;
        uint32_t points_per_word = (diff == DIFF_EASY) ? 10 : ((diff == DIFF_MEDIUM) ? 15 : 25);

        uint32_t score = 0;
        uint32_t time_left = 60;
        int hp = 3;
        int monster_y = 0;
        uint32_t rng_state = (uint32_t)rtc_read_second() * 17u + 3u;
        int current_word_idx = (int)(rng_state % (uint32_t)word_count);
        int laser_active = 0;
        uint32_t laser_hold = 0;
        char typed_buf[WORD_MAX_LEN];
        size_t typed_len = 0;
        typed_buf[0] = '\0';

        const char* status_msg = "Siap!";
        uint8_t status_color = 0x08;

        render_minimal_arena(
            player_name, diff, word_pool[current_word_idx], typed_buf,
            score, vfs_score_record, time_left, hp, monster_y, laser_active,
            status_msg, status_color
        );

        uint8_t last_rtc_sec = rtc_read_second();
        uint32_t monster_sec_counter = 0;

        /* Loop Arena Pertempuran */
        while (1) {
            char c = keyboard_poll_char();
            int need_redraw = 0;

            if (c == 27) { /* ESC -> kembali ke Menu Persiapan */
                break;
            }

            if (hp > 0 && time_left > 0) {
                if (c == '\b') {
                    if (typed_len > 0) {
                        typed_len--;
                        typed_buf[typed_len] = '\0';
                        need_redraw = 1;
                    }
                } else if (c >= ' ' && c <= '~' && c != ' ') {
                    rng_state = rng_state * 1103515245u + 12345u + (uint32_t)c;
                    if (typed_len < WORD_MAX_LEN - 1) {
                        typed_buf[typed_len++] = c;
                        typed_buf[typed_len] = '\0';
                        need_redraw = 1;
                    }

                    /* Jika kata target selesai diketik dengan benar */
                    if (game_streq(typed_buf, word_pool[current_word_idx])) {
                        score += points_per_word;
                        if (score > high_score) {
                            high_score = score;
                            save_highscore_to_vfs(high_score, player_name, diff);
                        }

                        /* Dorong monster naik kembali sesuai tingkat kesulitan */
                        if (monster_y >= pushback_steps) {
                            monster_y -= pushback_steps;
                        } else {
                            monster_y = 0;
                        }
                        monster_sec_counter = 0;
                        laser_active = 1;
                        laser_hold = 400;
                        status_msg = "Tembakan Telak!";
                        status_color = 0x0A;

                        current_word_idx = (int)((rng_state >> 4) % (uint32_t)word_count);
                        typed_len = 0;
                        typed_buf[0] = '\0';
                        need_redraw = 1;
                    }
                }

                for (int d = 0; d < 400; d++) {
                    (void)game_inb(0x80);
                }

                if (laser_hold > 0) {
                    laser_hold--;
                    if (laser_hold == 0) {
                        laser_active = 0;
                        need_redraw = 1;
                    }
                }

                uint8_t cur_sec = rtc_read_second();
                if (cur_sec != 0xFF && cur_sec != last_rtc_sec) {
                    last_rtc_sec = cur_sec;

                    if (time_left > 0) {
                        time_left--;
                    }

                    monster_sec_counter++;
                    if (monster_sec_counter >= step_seconds) {
                        monster_sec_counter = 0;
                        monster_y++;

                        if (monster_y > 9) {
                            hp--;
                            monster_y = 0;
                            typed_len = 0;
                            typed_buf[0] = '\0';
                            current_word_idx = (current_word_idx + 1) % word_count;
                            status_msg = "Benteng Tertabrak!";
                            status_color = 0x0C;
                        } else if (!laser_active) {
                            status_msg = "Monster mendekat";
                            status_color = (monster_y >= 7) ? 0x0C : 0x08;
                        }
                    }

                    if (hp <= 0) {
                        status_msg = "KALAH! [ENTER] Ulang";
                        status_color = 0x0C;
                    } else if (time_left == 0) {
                        status_msg = "MENANG! [ENTER] Ulang";
                        status_color = 0x0A;
                    }

                    need_redraw = 1;
                }

                if (need_redraw) {
                    render_minimal_arena(
                        player_name, diff, word_pool[current_word_idx], typed_buf,
                        score, vfs_score_record, time_left, hp, monster_y, laser_active,
                        status_msg, status_color
                    );
                }
            } else {
                if (c == '\n') {
                    break; /* Kembali ke menu persiapan untuk main lagi */
                }
            }
        }
    }
}
