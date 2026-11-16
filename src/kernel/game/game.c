#include "kernel/game/game.h"
#include "drivers/keyboard.h"
#include "kernel/06_Alokator_Memori_Dinamis/heap.h"
#include "kernel/07_Virtual_File_System/vfs.h"

#define ARENA_WIDTH   80
#define ARENA_HEIGHT  25
#define MAX_WORD_POOL 160
#define WORD_MAX_LEN  20
#define NAME_MAX_LEN  16

/* Batas kolom jendela kiri (Win 1: Game) dan jendela kanan (Win 2 & 3: Memori) */
#define WIN1_LEFT     0
#define WIN1_RIGHT    48
#define WIN2_LEFT     49
#define WIN2_RIGHT    79

#define MAX_MEM_LOGS  7
#define LOG_TEXT_LEN  28
#define MAX_EXTRA_BLK 12
#define MAX_KILL_LOGS 32

extern void vga_render(void);
extern void vga_print(const char* str, uint8_t color);

static volatile uint16_t* const vga_mem = (volatile uint16_t*)0xB8000;
static volatile uint16_t arena_buf[ARENA_HEIGHT][ARENA_WIDTH];

/* Karakter CP437 untuk UI Multi-Window */
#define LINE_H  ((char)0xC4) /* ─ */
#define LINE_V  ((char)0xB3) /* │ */
#define BOX_TL  ((char)0xDA) /* ┌ */
#define BOX_TR  ((char)0xBF) /* ┐ */
#define BOX_BL  ((char)0xC0) /* └ */
#define BOX_BR  ((char)0xD9) /* ┘ */
#define SYM_HRT ((char)0x03) /* ♥ */
#define SYM_UP  ((char)0x1E) /* ▲ */
#define BAR_FULL ((char)0xDB) /* █ */
#define BAR_EMPTY ((char)0xB0) /* ░ */

typedef enum {
    DIFF_EASY = 0,
    DIFF_MEDIUM = 1,
    DIFF_HARD = 2
} difficulty_t;

/* Struktur objek sesi yang dialokasikan secara dinamis di Heap */
typedef struct {
    char player_name[NAME_MAX_LEN];
    difficulty_t diff;
    uint32_t high_score;
    char score_record[MAX_FILESIZE];
    uint8_t padding[192];
} game_session_t;

/* Struktur buffer berkas VFS yang dialokasikan di Heap */
typedef struct {
    char raw_words[MAX_FILESIZE];
    char scratch[MAX_FILESIZE];
} game_vfs_buf_t;

/* Struktur kolam kata soal yang dialokasikan di Heap (3200 bytes) */
typedef struct {
    char words[MAX_WORD_POOL][WORD_MAX_LEN];
} game_word_pool_t;

/* Struktur entitas monster aktif yang dialokasikan tiap spawn */
typedef struct {
    char word[WORD_MAX_LEN];
    int y_pos;
    uint32_t points;
    uint8_t sprite_cache[220];
} monster_entity_t;

/* Struktur catatan riwayat tembakan/kill yang dialokasikan tiap monster kalah */
typedef struct {
    char word[WORD_MAX_LEN];
    uint32_t score_after;
    uint8_t telemetry[480];
} kill_record_t;

/* State pointer alokasi dinamis untuk dipantau di Window 2 & 3 */
static size_t baseline_used_bytes = 0;
static game_session_t*   g_session   = NULL;
static game_vfs_buf_t*   g_vfs_buf   = NULL;
static game_word_pool_t* g_word_pool = NULL;
static uint8_t*          g_arena_fx  = NULL;
static monster_entity_t* g_monster   = NULL;

static kill_record_t*    g_kill_logs[MAX_KILL_LOGS];
static int               g_kill_log_count = 0;

static void*             g_extra_blocks[MAX_EXTRA_BLK];
static int               g_extra_count = 0;

/* Ring buffer untuk log alokasi/dealokasi (Window 3) */
static char    mem_log_lines[MAX_MEM_LOGS][LOG_TEXT_LEN];
static uint8_t mem_log_colors[MAX_MEM_LOGS];
static int     mem_log_count = 0;

static inline void game_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t game_inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void delay_io_cycles(uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; i++) {
        (void)game_inb(0x80);
    }
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

static void arena_puts_lim(int row, int col, const char* str, uint8_t color, int max_col) {
    int c = col;
    while (*str != '\0' && c <= max_col && c < ARENA_WIDTH) {
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

/* Mencatat aktivitas kmalloc / kfree ke Window 3 (Log Alokasi) */
static void mem_log_push(char op, const char* tag, size_t bytes, uint8_t color) {
    char line[LOG_TEXT_LEN];
    int idx = 0;

    line[idx++] = op; /* '+' atau '-' atau '!' */
    for (int i = 0; tag[i] != '\0' && idx < 17; i++) {
        line[idx++] = tag[i];
    }
    while (idx < 17) {
        line[idx++] = ' ';
    }

    if (bytes > 0) {
        char num[12];
        int n = 0;
        size_t tmp = bytes;
        while (tmp > 0 && n < 10) {
            num[n++] = (char)('0' + (tmp % 10));
            tmp /= 10;
        }
        while (n > 0 && idx < LOG_TEXT_LEN - 2) {
            line[idx++] = num[--n];
        }
        if (idx < LOG_TEXT_LEN - 1) {
            line[idx++] = 'B';
        }
    }
    line[idx] = '\0';

    if (mem_log_count < MAX_MEM_LOGS) {
        for (int c = 0; c < LOG_TEXT_LEN; c++) {
            mem_log_lines[mem_log_count][c] = line[c];
        }
        mem_log_colors[mem_log_count] = color;
        mem_log_count++;
    } else {
        for (int r = 1; r < MAX_MEM_LOGS; r++) {
            for (int c = 0; c < LOG_TEXT_LEN; c++) {
                mem_log_lines[r - 1][c] = mem_log_lines[r][c];
            }
            mem_log_colors[r - 1] = mem_log_colors[r];
        }
        for (int c = 0; c < LOG_TEXT_LEN; c++) {
            mem_log_lines[MAX_MEM_LOGS - 1][c] = line[c];
        }
        mem_log_colors[MAX_MEM_LOGS - 1] = color;
    }
}

/* Wrapper kmalloc & kfree yang langsung mencatat ke log visual */
static void* game_alloc_logged(size_t size, const char* label) {
    void* ptr = kmalloc(size);
    if (ptr != NULL) {
        mem_log_push('+', label, size, 0x0A);
    } else {
        mem_log_push('!', "OOM Gagal Alokasi", size, 0x0C);
    }
    return ptr;
}

static void game_free_logged(void* ptr, size_t size, const char* label) {
    if (ptr != NULL) {
        kfree(ptr);
        mem_log_push('-', label, size, 0x0E);
    }
}

/* Fungsi untuk menambah/mengurangi beban memori uji secara interaktif (PGUP / PGDN) */
static void handle_interactive_memory_key(char c, int* changed_out) {
    if (c == KEY_PGUP) {
        if (g_extra_count < MAX_EXTRA_BLK) {
            void* blk = game_alloc_logged(4096, "alloc UjiHeap4K");
            if (blk != NULL) {
                g_extra_blocks[g_extra_count++] = blk;
            }
        } else {
            mem_log_push('!', "Maks Blok Uji(12)", 0, 0x0C);
        }
        if (changed_out) *changed_out = 1;
    } else if (c == KEY_PGDN) {
        if (g_extra_count > 0) {
            g_extra_count--;
            game_free_logged(g_extra_blocks[g_extra_count], 4096, "free  UjiHeap4K");
            g_extra_blocks[g_extra_count] = NULL;
        } else {
            mem_log_push('!', "Blok Uji Kosong", 0, 0x08);
        }
        if (changed_out) *changed_out = 1;
    }
}

/* Merender Top Bar, Window 2 (Live Heap Monitor), Window 3 (Log Alokasi), dan Bottom Bar */
static void render_multi_window_chrome(int active_win) {
    /* Baris 0: Judul Desktop Multi-Window */
    for (int c = 0; c < ARENA_WIDTH; c++) {
        arena_putc(0, c, ' ', 0x1F);
    }
    arena_puts(0, 1, "GIBRANOS MULTI-WINDOW", 0x1E);
    arena_puts(0, 24, "| Win1: Game Typing Defense | Win2-3: Live Heap Monitor", 0x1F);

    /* Kerangka Window 1 (Kiri: Game) */
    arena_box(1, WIN1_LEFT, 23, WIN1_RIGHT, (active_win == 1) ? 0x0B : 0x08);
    arena_puts(1, 2, "[ WIN 1: GAME TYPING DEFENSE ]", 0x0B);

    /* Kerangka Window 2 (Kanan Atas: Heap Memory Monitor) */
    arena_box(1, WIN2_LEFT, 14, WIN2_RIGHT, 0x0E);
    arena_puts(1, 51, "[ WIN 2: HEAP MONITOR ]", 0x0E);

    size_t total_mem = HEAP_SIZE;
    size_t free_mem  = heap_get_free_bytes();
    size_t used_mem  = total_mem - free_mem;
    uint32_t pct     = (uint32_t)((used_mem * 100u) / total_mem);

    /* Hitung jumlah blok aktif dan kosong dari linked list heap */
    uint32_t used_blocks = 0;
    uint32_t free_blocks = 0;
    heap_block_t* curr = heap_get_head();
    while (curr != NULL) {
        if (curr->free) free_blocks++;
        else used_blocks++;
        curr = curr->next;
    }

    arena_puts(2, 51, "Total Heap : ", 0x07);
    int len = arena_put_uint(2, 64, (uint32_t)total_mem, 0x0F);
    arena_puts(2, 64 + len, " B", 0x07);

    arena_puts(3, 51, "Terpakai   : ", 0x07);
    len = arena_put_uint(3, 64, (uint32_t)used_mem, 0x0E);
    arena_puts(3, 64 + len, " B", 0x0E);

    arena_puts(4, 51, "Tersedia   : ", 0x07);
    len = arena_put_uint(4, 64, (uint32_t)free_mem, 0x0A);
    arena_puts(4, 64 + len, " B", 0x0A);

    arena_puts(5, 51, "Delta Game : ", 0x07);
    if (used_mem >= baseline_used_bytes) {
        size_t delta = used_mem - baseline_used_bytes;
        arena_putc(5, 64, '+', (delta > 0) ? 0x0A : 0x08);
        len = arena_put_uint(5, 65, (uint32_t)delta, (delta > 0) ? 0x0A : 0x08);
        arena_puts(5, 65 + len, " B", (delta > 0) ? 0x0A : 0x08);
    } else {
        size_t delta = baseline_used_bytes - used_mem;
        arena_putc(5, 64, '-', 0x0E);
        len = arena_put_uint(5, 65, (uint32_t)delta, 0x0E);
        arena_puts(5, 65 + len, " B", 0x0E);
    }

    arena_puts(6, 51, "Blok Heap  : ", 0x07);
    len = arena_put_uint(6, 64, used_blocks, 0x0B);
    arena_puts(6, 64 + len, " isi/", 0x08);
    len += 5 + arena_put_uint(6, 64 + len + 5, free_blocks, 0x07);
    arena_puts(6, 64 + len, " free", 0x08);

    /* Visual Bar Penggunaan Memori (16 segmen) */
    arena_puts(7, 51, "Beban [", 0x07);
    uint32_t filled = (uint32_t)((used_mem * 16u) / total_mem);
    if (used_mem > 16 && filled == 0) filled = 1;
    if (filled > 16) filled = 16;
    uint8_t bar_col = (pct >= 75) ? 0x0C : ((pct >= 35) ? 0x0E : 0x0A);
    for (uint32_t b = 0; b < 16; b++) {
        arena_putc(7, 58 + (int)b, (b < filled) ? BAR_FULL : BAR_EMPTY, (b < filled) ? bar_col : 0x08);
    }
    arena_putc(7, 74, ']', 0x07);
    len = arena_put_uint(7, 75, pct, 0x0F);
    arena_putc(7, 75 + len, '%', 0x07);

    /* Tabel Blok Heap Aktif */
    arena_hline(8, WIN2_LEFT + 1, WIN2_RIGHT - 1, 0x08);
    arena_puts(8, 51, " Peta Blok Heap (First-Fit) ", 0x0B);

    curr = heap_get_head();
    int row = 9;
    int idx = 1;
    while (curr != NULL && row <= 13) {
        arena_putc(row, 51, '#', 0x08);
        int ilen = arena_put_uint(row, 52, (uint32_t)idx, 0x07);
        arena_puts(row, 52 + ilen, curr->free ? " FREE " : " USED ", curr->free ? 0x08 : 0x0A);
        int slen = arena_put_uint(row, 59, (uint32_t)curr->size, curr->free ? 0x07 : 0x0F);
        arena_puts(row, 59 + slen, "B", 0x08);

        if (!curr->free) {
            const char* owner = "Data";
            uint8_t* data_ptr = (uint8_t*)curr + sizeof(heap_block_t);
            if (data_ptr == (uint8_t*)g_session)        owner = "(Session)";
            else if (data_ptr == (uint8_t*)g_vfs_buf)   owner = "(VFSBuf)";
            else if (data_ptr == (uint8_t*)g_word_pool) owner = "(WordPool)";
            else if (data_ptr == (uint8_t*)g_arena_fx)  owner = "(SpriteFX)";
            else if (data_ptr == (uint8_t*)g_monster)   owner = "(Monster)";
            else if (curr->size == 4096)                owner = "(Uji4KB)";
            else if (curr->size >= 480)                 owner = "(KillLog)";
            arena_puts_lim(row, 67, owner, 0x0E, WIN2_RIGHT - 1);
        } else {
            arena_puts_lim(row, 67, "(Tersedia)", 0x08, WIN2_RIGHT - 1);
        }

        curr = curr->next;
        row++;
        idx++;
    }

    /* Kerangka Window 3 (Kanan Bawah: Log Alokasi Dinamis) */
    arena_box(15, WIN2_LEFT, 23, WIN2_RIGHT, 0x0A);
    arena_puts(15, 51, "[ WIN 3: LOG KMALLOC/KFREE ]", 0x0A);

    for (int i = 0; i < mem_log_count && i < MAX_MEM_LOGS; i++) {
        arena_puts_lim(16 + i, 51, mem_log_lines[i], mem_log_colors[i], WIN2_RIGHT - 1);
    }

    /* Baris 24: Status & Shortcut Multi-Window */
    arena_puts(24, 1, "[ESC] Keluar/Free  [PGUP] +Alokasi 4KB  [PGDN] -Free 4KB  [ENTER] Pilih", 0x07);
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
    if (g_session != NULL) {
        for (int i = 0; i <= idx; i++) {
            g_session->score_record[i] = buf[i];
        }
    }
}

/* Memuat kata dari words.txt di VFS ke kolam kata di Heap */
static int load_words_for_difficulty(difficulty_t diff) {
    int count = 0;
    if (g_vfs_buf == NULL || g_word_pool == NULL) {
        return 0;
    }

    if (vfs_read("words.txt", g_vfs_buf->raw_words) != VFS_OK) {
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
    const char* raw = g_vfs_buf->raw_words;
    while (raw[i] != '\0' && count < MAX_WORD_POOL) {
        while (raw[i] == ' ' || raw[i] == '\n' || raw[i] == '\r' || raw[i] == '\t') {
            i++;
        }
        if (raw[i] == '\0') break;

        char temp[WORD_MAX_LEN];
        size_t w = 0;
        while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\n' &&
               raw[i] != '\r' && raw[i] != '\t') {
            if (w < WORD_MAX_LEN - 1) {
                temp[w++] = raw[i];
            }
            i++;
        }
        temp[w] = '\0';

        if (w >= min_len && w <= max_len) {
            for (size_t k = 0; k <= w; k++) {
                g_word_pool->words[count][k] = temp[k];
            }
            count++;
        }
    }

    if (count == 0) {
        i = 0;
        while (raw[i] != '\0' && count < MAX_WORD_POOL) {
            while (raw[i] == ' ' || raw[i] == '\n' || raw[i] == '\r' || raw[i] == '\t') {
                i++;
            }
            if (raw[i] == '\0') break;

            size_t w = 0;
            while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\n' &&
                   raw[i] != '\r' && raw[i] != '\t' && w < WORD_MAX_LEN - 1) {
                g_word_pool->words[count][w++] = raw[i++];
            }
            g_word_pool->words[count][w] = '\0';
            if (w > 0) count++;
        }
    }

    return count;
}

/* Render Window 1 saat proses pemuatan (Loading Game) agar perubahan memori terlihat jelas */
static void render_loading_stage(int step, const char* status_text) {
    arena_clear(0x07);
    render_multi_window_chrome(1);

    arena_puts(3, 8, "MEMUAT RESOURCE GAME KE HEAP", 0x0E);
    arena_hline(4, 2, WIN1_RIGHT - 2, 0x08);

    arena_puts(6, 3, "Perhatikan [WIN 2] & [WIN 3] di kanan:", 0x07);
    arena_puts(7, 3, "Setiap modul game dialokasikan dinamis", 0x08);
    arena_puts(8, 3, "menggunakan kmalloc() pada Heap Kernel.", 0x08);

    arena_puts(10, 3, (step >= 1) ? "[OK] 1. Session State     (736 B)" : "[..] 1. Session State     (736 B)",
               (step >= 1) ? 0x0A : 0x08);
    arena_puts(12, 3, (step >= 2) ? "[OK] 2. Buffer Berkas VFS (1024 B)" : "[..] 2. Buffer Berkas VFS (1024 B)",
               (step >= 2) ? 0x0A : 0x08);
    arena_puts(14, 3, (step >= 3) ? "[OK] 3. Kolam Kata Soal   (3200 B)" : "[..] 3. Kolam Kata Soal   (3200 B)",
               (step >= 3) ? 0x0A : 0x08);
    arena_puts(16, 3, (step >= 4) ? "[OK] 4. Buffer Efek Arena (2048 B)" : "[..] 4. Buffer Efek Arena (2048 B)",
               (step >= 4) ? 0x0A : 0x08);

    /* Progress bar loading di Window 1 */
    arena_puts(19, 3, "Progres Muat: [", 0x07);
    for (int i = 0; i < 20; i++) {
        int filled = (step * 5);
        arena_putc(19, 18 + i, (i < filled) ? BAR_FULL : BAR_EMPTY, (i < filled) ? 0x0B : 0x08);
    }
    arena_putc(19, 38, ']', 0x07);

    arena_puts_lim(21, 3, status_text, (step >= 4) ? 0x0A : 0x0E, WIN1_RIGHT - 2);
    arena_present();
}

/* Render Menu Persiapan di dalam Window 1 */
static void render_setup_menu_win1(const char* player_name, difficulty_t diff, const char* high_record) {
    arena_clear(0x07);
    render_multi_window_chrome(1);

    arena_puts(3, 11, "GIBRANOS TYPING DEFENSE", 0x0B);
    arena_hline(4, 2, WIN1_RIGHT - 2, 0x08);

    /* Input Nama User */
    arena_puts(6, 3, "Nama Pemain :", 0x07);
    arena_puts(6, 17, "[ ", 0x08);
    arena_puts(6, 19, player_name, 0x0F);
    arena_putc(6, 19 + (int)game_strlen(player_name), '_', 0x0A);
    arena_puts(6, 36, "]", 0x08);

    /* Pilihan Tingkat Kesulitan */
    arena_puts(8, 3, "Kesulitan (Panah Atas/Bawah / TAB):", 0x07);

    arena_puts(10, 4, (diff == DIFF_EASY)   ? "> [MUDAH]  3-5 huruf | Lambat (4d)" : "  [MUDAH]  3-5 huruf | Lambat (4d)",
               (diff == DIFF_EASY)   ? 0x0A : 0x08);
    arena_puts(11, 4, (diff == DIFF_MEDIUM) ? "> [SEDANG] 6-8 huruf | Sedang (2d)" : "  [SEDANG] 6-8 huruf | Sedang (2d)",
               (diff == DIFF_MEDIUM) ? 0x0E : 0x08);
    arena_puts(12, 4, (diff == DIFF_HARD)   ? "> [SULIT]  9+ huruf  | Cepat  (1d)" : "  [SULIT]  9+ huruf  | Cepat  (1d)",
               (diff == DIFF_HARD)   ? 0x0C : 0x08);

    arena_hline(14, 2, WIN1_RIGHT - 2, 0x08);
    arena_puts(15, 3, "Rekor VFS :", 0x08);
    arena_puts_lim(15, 15, high_record, 0x0B, WIN1_RIGHT - 2);

    arena_hline(17, 2, WIN1_RIGHT - 2, 0x08);
    arena_puts(18, 3, "Simulasi Memori Langsung:", 0x0E);
    arena_puts(19, 3, "Tekan [PGUP] alokasi +4KB ke Heap", 0x07);
    arena_puts(20, 3, "Tekan [PGDN] bebaskan -4KB dari Heap", 0x07);

    arena_puts(22, 3, "[ENTER] Mulai Main", 0x0A);
    arena_puts(22, 26, "[ESC] Keluar & Free", 0x0C);

    arena_present();
}

/* Render Arena Pertempuran di dalam Window 1 + Update Live Memory di Window 2 & 3 */
static void render_arena_win1(
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
    render_multi_window_chrome(1);

    /* Baris Info Arena (Row 2 & 3 di dalam Window 1) */
    arena_puts(2, 2, "USER:", 0x08);
    arena_puts_lim(2, 7, player_name, 0x0F, 18);

    arena_puts(2, 19, "LVL:", 0x08);
    uint8_t dcol = (diff == DIFF_EASY) ? 0x0A : ((diff == DIFF_MEDIUM) ? 0x0E : 0x0C);
    arena_puts(2, 23, diff_to_name(diff), dcol);

    arena_puts(2, 31, "SKOR:", 0x08);
    arena_put_uint(2, 36, score, 0x0A);

    arena_puts(3, 2, "WAKTU:", 0x08);
    int tlen = arena_put_uint(3, 8, time_left, (time_left <= 10) ? 0x0C : 0x0F);
    arena_putc(3, 8 + tlen, 's', 0x08);

    arena_puts(3, 15, "HP:", 0x08);
    for (int i = 0; i < 3; i++) {
        arena_putc(3, 18 + i, SYM_HRT, (i < hp) ? 0x0C : 0x08);
    }

    arena_puts(3, 24, "KILL LOG:", 0x08);
    int klen = arena_put_uint(3, 33, (uint32_t)g_kill_log_count, 0x0E);
    arena_puts(3, 33 + klen, " blok", 0x08);

    arena_hline(4, 1, WIN1_RIGHT - 1, 0x08);

    /* Area Pertempuran (Rows 5..16, titik tengah kolom = 24) */
    int m_row = 5 + monster_y;
    if (m_row > 12) m_row = 12;

    if (laser_active) {
        for (int r = m_row + 3; r <= 15; r++) {
            arena_putc(r, 23, LINE_V, 0x0B);
            arena_putc(r, 24, SYM_UP, 0x0E);
        }
    }

    uint8_t m_color = (monster_y >= 6) ? 0x0C : ((monster_y >= 3) ? 0x0E : 0x0A);
    arena_puts(m_row,     20, " /\\_/\\ ", m_color);
    arena_puts(m_row + 1, 20, "( o.o )", m_color);
    arena_puts(m_row + 2, 20, " > ^ < ", m_color);

    /* Benteng & Pahlawan (Row 16-17) */
    arena_hline(16, 4, WIN1_RIGHT - 4, 0x08);
    arena_puts(16, 21, "[ ", 0x08);
    arena_putc(16, 23, SYM_UP, laser_active ? 0x0E : 0x0B);
    arena_puts(16, 24, " ]", 0x08);
    arena_puts(17, 21, " \\O/ ", 0x0B);

    arena_puts_lim(17, 2, status_msg, status_color, 20);
    arena_hline(18, 1, WIN1_RIGHT - 1, 0x08);

    /* Area Ketikan di Window 1 (Rows 19..22) */
    arena_puts(19, 2, "TARGET :", 0x07);
    size_t t_len = game_strlen(target_word);
    size_t u_len = game_strlen(typed_buf);
    int prefix_ok = 1;

    for (size_t i = 0; i < t_len && (11 + (int)(i * 2)) < WIN1_RIGHT - 1; i++) {
        uint8_t ch_color = 0x0F;
        if (i < u_len) {
            if (prefix_ok && typed_buf[i] == target_word[i]) {
                ch_color = 0x0A;
            } else {
                prefix_ok = 0;
                ch_color = 0x0C;
            }
        }
        arena_putc(19, 11 + (int)(i * 2), target_word[i], ch_color);
    }

    arena_puts(21, 2, "KETIK  : > ", 0x08);
    arena_puts_lim(21, 13, typed_buf, prefix_ok ? 0x0A : 0x0C, WIN1_RIGHT - 3);
    if (13 + (int)u_len < WIN1_RIGHT - 1) {
        arena_putc(21, 13 + (int)u_len, '_', 0x0F);
    }

    arena_puts(22, 2, "Rekor:", 0x08);
    arena_puts_lim(22, 9, high_record, 0x07, WIN1_RIGHT - 2);

    arena_present();
}

/* Membebaskan seluruh log kill & monster saat ronde berakhir */
static void free_round_allocations(void) {
    if (g_monster != NULL) {
        game_free_logged(g_monster, sizeof(monster_entity_t), "free  Monster");
        g_monster = NULL;
    }
    for (int i = g_kill_log_count - 1; i >= 0; i--) {
        if (g_kill_logs[i] != NULL) {
            game_free_logged(g_kill_logs[i], sizeof(kill_record_t), "free  KillLog");
            g_kill_logs[i] = NULL;
        }
    }
    g_kill_log_count = 0;
}

/* Membebaskan seluruh memori game saat keluar ke CLI */
static void free_all_game_allocations(void) {
    free_round_allocations();

    while (g_extra_count > 0) {
        g_extra_count--;
        game_free_logged(g_extra_blocks[g_extra_count], 4096, "free  UjiHeap4K");
        g_extra_blocks[g_extra_count] = NULL;
    }

    if (g_arena_fx != NULL) {
        game_free_logged(g_arena_fx, 2048, "free  SpriteFX");
        g_arena_fx = NULL;
    }
    if (g_word_pool != NULL) {
        game_free_logged(g_word_pool, sizeof(game_word_pool_t), "free  WordPool");
        g_word_pool = NULL;
    }
    if (g_vfs_buf != NULL) {
        game_free_logged(g_vfs_buf, sizeof(game_vfs_buf_t), "free  VFSBuf");
        g_vfs_buf = NULL;
    }
    if (g_session != NULL) {
        game_free_logged(g_session, sizeof(game_session_t), "free  Session");
        g_session = NULL;
    }
}

/* Mengalokasikan objek monster baru di Heap untuk kata soal berikutnya */
static void spawn_new_monster(const char* word, uint32_t points) {
    if (g_monster != NULL) {
        game_free_logged(g_monster, sizeof(monster_entity_t), "free  Monster");
        g_monster = NULL;
    }
    g_monster = (monster_entity_t*)game_alloc_logged(sizeof(monster_entity_t), "alloc Monster");
    if (g_monster != NULL) {
        size_t i = 0;
        while (word[i] != '\0' && i < WORD_MAX_LEN - 1) {
            g_monster->word[i] = word[i];
            i++;
        }
        g_monster->word[i] = '\0';
        g_monster->y_pos = 0;
        g_monster->points = points;
    }
}

void game_start(void) {
    baseline_used_bytes = HEAP_SIZE - heap_get_free_bytes();
    mem_log_count = 0;
    g_extra_count = 0;
    g_kill_log_count = 0;

    hide_vga_cursor();

    /*
     * TAHAP PEMUATAN MULTI-WINDOW:
     * Menampilkan alokasi bertahap agar pengguna dapat mengamati
     * kenaikan penggunaan memori secara langsung pada Window 2 & Window 3.
     */
    mem_log_push('*', "Mulai Muat Game", 0, 0x0B);
    render_loading_stage(0, "Kondisi awal Heap sebelum alokasi...");
    delay_io_cycles(250000);

    g_session = (game_session_t*)game_alloc_logged(sizeof(game_session_t), "alloc Session");
    render_loading_stage(1, "Mengalokasikan Session State di Heap...");
    delay_io_cycles(250000);

    g_vfs_buf = (game_vfs_buf_t*)game_alloc_logged(sizeof(game_vfs_buf_t), "alloc VFSBuf");
    render_loading_stage(2, "Mengalokasikan Buffer VFS di Heap...");
    delay_io_cycles(250000);

    g_word_pool = (game_word_pool_t*)game_alloc_logged(sizeof(game_word_pool_t), "alloc WordPool");
    render_loading_stage(3, "Mengalokasikan Kolam Kata di Heap...");
    delay_io_cycles(250000);

    g_arena_fx = (uint8_t*)game_alloc_logged(2048, "alloc SpriteFX");
    render_loading_stage(4, "Semua modul siap! Tekan ENTER / tunggu...");
    delay_io_cycles(350000);

    if (g_session == NULL || g_vfs_buf == NULL || g_word_pool == NULL) {
        free_all_game_allocations();
        vga_render();
        vga_print("[GAME] Gagal memuat game: Out of Memory pada Heap.\n", 0x0C);
        return;
    }

    const char* default_name = "Player";
    for (int i = 0; i < 7; i++) {
        g_session->player_name[i] = default_name[i];
    }
    size_t name_len = 6;
    g_session->diff = DIFF_EASY;

    if (vfs_read("scores.txt", g_session->score_record) != VFS_OK ||
        g_session->score_record[0] == '\0') {
        g_session->score_record[0] = '0';
        g_session->score_record[1] = '\0';
    }
    g_session->high_score = str_to_uint(g_session->score_record);

    /* Loop Menu Persiapan di Window 1 (Window 2 & 3 tetap live) */
    while (1) {
        render_setup_menu_win1(g_session->player_name, g_session->diff, g_session->score_record);

        int start_game = 0;
        while (1) {
            char c = keyboard_poll_char();
            if (c == 0) {
                __asm__ volatile ("pause");
                continue;
            }

            int mem_changed = 0;
            handle_interactive_memory_key(c, &mem_changed);
            if (mem_changed) {
                render_setup_menu_win1(g_session->player_name, g_session->diff, g_session->score_record);
                continue;
            }

            if (c == 27) { /* ESC -> Bebaskan seluruh memori & kembali ke CLI */
                free_all_game_allocations();
                render_loading_stage(0, "Membebaskan seluruh memori Heap (kfree)...");
                delay_io_cycles(250000);
                vga_render();
                vga_print("[GAME] Keluar dari Multi-Window Game. Seluruh alokasi Heap telah di-kfree.\n", 0x0A);
                return;
            } else if (c == KEY_UP) {
                if (g_session->diff > DIFF_EASY) {
                    g_session->diff = (difficulty_t)(g_session->diff - 1);
                }
                render_setup_menu_win1(g_session->player_name, g_session->diff, g_session->score_record);
            } else if (c == KEY_DOWN || c == '\t') {
                if (g_session->diff < DIFF_HARD) {
                    g_session->diff = (difficulty_t)(g_session->diff + 1);
                } else if (c == '\t') {
                    g_session->diff = DIFF_EASY;
                }
                render_setup_menu_win1(g_session->player_name, g_session->diff, g_session->score_record);
            } else if (c == '\b') {
                if (name_len > 0) {
                    name_len--;
                    g_session->player_name[name_len] = '\0';
                    render_setup_menu_win1(g_session->player_name, g_session->diff, g_session->score_record);
                }
            } else if (c == '\n') {
                if (name_len == 0) {
                    for (int i = 0; default_name[i] != '\0'; i++) {
                        g_session->player_name[i] = default_name[i];
                    }
                    g_session->player_name[6] = '\0';
                    name_len = 6;
                }
                start_game = 1;
                break;
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_') {
                if (name_len < NAME_MAX_LEN - 1) {
                    g_session->player_name[name_len++] = c;
                    g_session->player_name[name_len] = '\0';
                    render_setup_menu_win1(g_session->player_name, g_session->diff, g_session->score_record);
                }
            }
        }

        if (!start_game) break;

        int word_count = load_words_for_difficulty(g_session->diff);
        if (word_count <= 0) {
            const char* fallback = "kernel";
            for (int i = 0; i < 7; i++) g_word_pool->words[0][i] = fallback[i];
            word_count = 1;
        }

        difficulty_t diff = g_session->diff;
        uint32_t step_seconds    = (diff == DIFF_EASY) ? 4 : ((diff == DIFF_MEDIUM) ? 2 : 1);
        int pushback_steps       = (diff == DIFF_EASY) ? 3 : 2;
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

        /* Alokasi entitas monster pertama di Heap */
        spawn_new_monster(g_word_pool->words[current_word_idx], points_per_word);

        const char* status_msg = "Monster Spawn!";
        uint8_t status_color = 0x0A;

        render_arena_win1(
            g_session->player_name, diff, g_word_pool->words[current_word_idx], typed_buf,
            score, g_session->score_record, time_left, hp, monster_y, laser_active,
            status_msg, status_color
        );

        uint8_t last_rtc_sec = rtc_read_second();
        uint32_t monster_sec_counter = 0;

        /* Loop Arena Pertempuran */
        while (1) {
            char c = keyboard_poll_char();
            int need_redraw = 0;

            if (c == 27) { /* ESC -> Bebaskan memori ronde & kembali ke Menu */
                free_round_allocations();
                break;
            }

            handle_interactive_memory_key(c, &need_redraw);

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
                    if (game_streq(typed_buf, g_word_pool->words[current_word_idx])) {
                        score += points_per_word;
                        if (score > g_session->high_score) {
                            g_session->high_score = score;
                            save_highscore_to_vfs(g_session->high_score, g_session->player_name, diff);
                        }

                        /* Alokasikan Kill Log baru di Heap agar perubahan memori terlihat saat bermain */
                        if (g_kill_log_count < MAX_KILL_LOGS) {
                            kill_record_t* rec = (kill_record_t*)game_alloc_logged(
                                sizeof(kill_record_t), "alloc KillLog"
                            );
                            if (rec != NULL) {
                                rec->score_after = score;
                                g_kill_logs[g_kill_log_count++] = rec;
                            }
                        }

                        if (monster_y >= pushback_steps) {
                            monster_y -= pushback_steps;
                        } else {
                            monster_y = 0;
                        }
                        monster_sec_counter = 0;
                        laser_active = 1;
                        laser_hold = 400;
                        status_msg = "Telak! +KillLog";
                        status_color = 0x0A;

                        current_word_idx = (int)((rng_state >> 4) % (uint32_t)word_count);
                        spawn_new_monster(g_word_pool->words[current_word_idx], points_per_word);

                        typed_len = 0;
                        typed_buf[0] = '\0';
                        need_redraw = 1;
                    }
                }

                delay_io_cycles(400);

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

                        if (monster_y > 7) {
                            hp--;
                            monster_y = 0;
                            typed_len = 0;
                            typed_buf[0] = '\0';
                            current_word_idx = (current_word_idx + 1) % word_count;
                            spawn_new_monster(g_word_pool->words[current_word_idx], points_per_word);
                            status_msg = "Benteng Kena!";
                            status_color = 0x0C;
                        } else if (!laser_active) {
                            status_msg = "Monster Turun";
                            status_color = (monster_y >= 5) ? 0x0C : 0x08;
                        }
                    }

                    if (hp <= 0) {
                        status_msg = "KALAH! [ENTER]";
                        status_color = 0x0C;
                    } else if (time_left == 0) {
                        status_msg = "MENANG! [ENTER]";
                        status_color = 0x0A;
                    }

                    need_redraw = 1;
                }

                if (need_redraw) {
                    render_arena_win1(
                        g_session->player_name, diff, g_word_pool->words[current_word_idx], typed_buf,
                        score, g_session->score_record, time_left, hp, monster_y, laser_active,
                        status_msg, status_color
                    );
                }
            } else {
                if (c == '\n') {
                    free_round_allocations();
                    break;
                } else if (need_redraw) {
                    render_arena_win1(
                        g_session->player_name, diff, g_word_pool->words[current_word_idx], typed_buf,
                        score, g_session->score_record, time_left, hp, monster_y, laser_active,
                        status_msg, status_color
                    );
                }
            }
        }
    }
}
