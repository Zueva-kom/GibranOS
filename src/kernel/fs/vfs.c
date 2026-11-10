#include "kernel/fs/vfs.h"

/* Fungsi output layar VGA dari kernel/main.c */
extern void vga_print(const char* str, uint8_t color);
extern void vga_putchar(char c, uint8_t color);

/*
 * Fallback weak untuk pemeriksaan hak akses (modul security Anggota 4).
 * Jika modul security.c sudah di-link, fungsi di security.c akan menggantikan ini.
 */
__attribute__((weak)) int security_check_access(uint8_t file_protected, int is_write_op) {
    if (file_protected && is_write_op) {
        return 0; /* Tolak modifikasi/hapus berkas terproteksi secara default (mode guest) */
    }
    return 1;
}

/* Penyimpanan berkas di dalam memori RAM (RAMDisk) */
static ramdisk_file_t ramdisk[MAX_FILES];

/* Menghitung panjang string */
static uint32_t vfs_strlen(const char* str) {
    uint32_t len = 0;
    if (str == NULL) {
        return 0;
    }
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

/* Membandingkan dua string (1 jika sama, 0 jika berbeda) */
static int vfs_streq(const char* a, const char* b) {
    size_t i = 0;
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

/* Menyalin string dengan batas maksimum ukuran buffer */
static uint32_t vfs_strncpy(char* dest, const char* src, size_t max_len) {
    size_t i = 0;
    if (dest == NULL || max_len == 0) {
        return 0;
    }
    if (src == NULL) {
        dest[0] = '\0';
        return 0;
    }
    while (src[i] != '\0' && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return (uint32_t)i;
}

/* Mencetak angka desimal unsigned ke layar VGA */
static void vfs_print_uint(uint32_t num, uint8_t color) {
    char buf[12];
    int i = 0;

    if (num == 0) {
        vga_putchar('0', color);
        return;
    }

    while (num > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (num % 10));
        num /= 10;
    }

    while (i > 0) {
        vga_putchar(buf[--i], color);
    }
}

/* Mencari indeks berkas berdasarkan nama (-1 jika tidak ditemukan) */
static int find_file_index(const char* name) {
    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        if (ramdisk[i].in_use && vfs_streq(ramdisk[i].name, name)) {
            return i;
        }
    }
    return -1;
}

/* Mencari slot kosong di RAMDisk (-1 jika penuh) */
static int find_free_slot(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!ramdisk[i].in_use) {
            return i;
        }
    }
    return -1;
}

/* Inisialisasi RAMDisk dan membuat berkas bawaan (words.txt & scores.txt) */
void vfs_init(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        ramdisk[i].name[0] = '\0';
        ramdisk[i].data[0] = '\0';
        ramdisk[i].size = 0;
        ramdisk[i].in_use = 0;
        ramdisk[i].is_protected = 0;
    }

    /* Berkas sistem bawaan untuk Game Key-Typing (diproteksi, kata dari dataset random-10000-words) */
    vfs_create(
        "words.txt",
        /* Mudah (4-5 huruf) */
        "able acid actor adapt admin alarm album alert alien alpha "
        "aqua arena audio basic batch beat blade blank block logic "
        "brain brief cable cache chain chart chief claim clean cloud "
        "coach coral craft crash cream crown curve cycle daily dance "
        "debug delay depth draft dream drive earth elite entry equal "
        "error event exact extra faith fault fiber field final flame "
        "flash fleet focus frame fresh front giant glass globe grace "
        "grade grand grant green guard guide happy heart heavy horse "
        "hotel human ideal image index inner input issue joint judge "
        "laser layer level light limit local magic major maker match "
        "media metal micro model moral motor mount mouse music mutex "
        "night noble noise north novel nurse ocean offer order organ "
        "other outer panel paper party patch peace phase phone photo "
        "pilot pitch pixel place plain plane plant plate point power "
        "press price pride prime print prior prize proof proud pulse "
        "queen quick quiet radio range rapid ratio reach "
        /* Sedang (6-8 huruf) */
        "academy achieve acoustic adapter address advance already ancient "
        "anxiety awesome balance battery blocking builder cabinet caliber "
        "capture carrier catalog central century chamber channel chapter "
        "charity classic climate cluster collect college combine command "
        "compact compare complex concept confirm connect console control "
        "convert courage counter crystal culture current damage dataset "
        "default defense deliver density desktop develop digital display "
        "dynamic element emerald feature fighter finding forecast fortune "
        "freedom gallery gateway general gibranos gravity handler harmony "
        "history hosting impulse journey justice kernel kingdom landing "
        "library machine manager memory monster network package pattern "
        "pointer process program project protect quantum ramdisk request "
        "runtime scanner section service session signal storage system "
        "teacher thread "
        /* Sulit (9+ huruf) */
        "abandoned adjustable alternative appreciated attending "
        "beautifully calculation charitable collections complaints "
        "confirmed containers copyright definition discussion "
        "efficient exclusion functional guarantee hydraulic "
        "independent inspiration introducing migration navigator "
        "occurrence paragraphs perspective procedures protection "
        "regulation reservation scheduler subscriber syndicate "
        "thoroughly treasurer wilderness interrupt",
        1
    );
    vfs_create(
        "scores.txt",
        "0 - Belum Ada (MUDAH)",
        1
    );
}

/* Membuat berkas baru di RAMDisk */
int vfs_create(const char* name, const char* content, uint8_t is_protected) {
    if (name == NULL || name[0] == '\0') {
        return VFS_ERR_INVALID;
    }

    /* Pastikan berkas dengan nama yang sama belum ada */
    if (find_file_index(name) >= 0) {
        return VFS_ERR_EXISTS;
    }

    /* Cari slot kosong */
    int slot = find_free_slot();
    if (slot < 0) {
        return VFS_ERR_FULL;
    }

    vfs_strncpy(ramdisk[slot].name, name, MAX_FILENAME);
    ramdisk[slot].size = vfs_strncpy(ramdisk[slot].data, content, MAX_FILESIZE);
    ramdisk[slot].in_use = 1;
    ramdisk[slot].is_protected = is_protected ? 1 : 0;

    return VFS_OK;
}

/* Membaca isi berkas dari RAMDisk ke dalam out_buffer */
int vfs_read(const char* name, char* out_buffer) {
    if (name == NULL || name[0] == '\0' || out_buffer == NULL) {
        return VFS_ERR_INVALID;
    }

    int idx = find_file_index(name);
    if (idx < 0) {
        out_buffer[0] = '\0';
        return VFS_ERR_NOT_FOUND;
    }

    if (!security_check_access(ramdisk[idx].is_protected, 0)) {
        out_buffer[0] = '\0';
        return VFS_ERR_PROTECTED;
    }

    vfs_strncpy(out_buffer, ramdisk[idx].data, MAX_FILESIZE);
    return VFS_OK;
}

/* Memperbarui isi berkas yang sudah ada di RAMDisk */
int vfs_update(const char* name, const char* new_content) {
    if (name == NULL || name[0] == '\0' || new_content == NULL) {
        return VFS_ERR_INVALID;
    }

    int idx = find_file_index(name);
    if (idx < 0) {
        return VFS_ERR_NOT_FOUND;
    }

    /* Cek proteksi berkas untuk operasi tulis (write = 1) */
    if (!security_check_access(ramdisk[idx].is_protected, 1)) {
        return VFS_ERR_PROTECTED;
    }

    ramdisk[idx].size = vfs_strncpy(ramdisk[idx].data, new_content, MAX_FILESIZE);
    return VFS_OK;
}

/* Memperbarui isi berkas secara internal oleh Kernel (tanpa blokir guest) */
int vfs_kernel_update(const char* name, const char* new_content) {
    if (name == NULL || name[0] == '\0' || new_content == NULL) {
        return VFS_ERR_INVALID;
    }

    int idx = find_file_index(name);
    if (idx < 0) {
        return VFS_ERR_NOT_FOUND;
    }

    ramdisk[idx].size = vfs_strncpy(ramdisk[idx].data, new_content, MAX_FILESIZE);
    return VFS_OK;
}

/* Menghapus berkas dari RAMDisk */
int vfs_delete(const char* name) {
    if (name == NULL || name[0] == '\0') {
        return VFS_ERR_INVALID;
    }

    int idx = find_file_index(name);
    if (idx < 0) {
        return VFS_ERR_NOT_FOUND;
    }

    /* Cek proteksi berkas untuk operasi hapus (write = 1) */
    if (!security_check_access(ramdisk[idx].is_protected, 1)) {
        return VFS_ERR_PROTECTED;
    }

    ramdisk[idx].name[0] = '\0';
    ramdisk[idx].data[0] = '\0';
    ramdisk[idx].size = 0;
    ramdisk[idx].in_use = 0;
    ramdisk[idx].is_protected = 0;

    return VFS_OK;
}

/* Menampilkan daftar seluruh berkas aktif di RAMDisk (untuk perintah CLI: ls) */
void vfs_list_files(void) {
    int count = 0;

    vga_print("Daftar Berkas RAMDisk (VFS):\n", 0x0E);
    vga_print("  NAMA BERKAS                      UKURAN     STATUS\n", 0x0B);
    vga_print("  ----------------------------------------------------------\n", 0x08);

    for (int i = 0; i < MAX_FILES; i++) {
        if (ramdisk[i].in_use) {
            count++;
            vga_print("  ", 0x07);
            vga_print(ramdisk[i].name, 0x0F);

            /* Ratakan kolom nama berkas */
            uint32_t name_len = vfs_strlen(ramdisk[i].name);
            for (uint32_t s = name_len; s < MAX_FILENAME + 1; s++) {
                vga_putchar(' ', 0x07);
            }

            /* Cetak ukuran berkas dalam byte */
            vfs_print_uint(ramdisk[i].size, 0x07);
            vga_print(" B", 0x07);

            /* Ratakan kolom ukuran */
            uint32_t temp = ramdisk[i].size;
            uint32_t digits = (temp == 0) ? 1 : 0;
            while (temp > 0) {
                digits++;
                temp /= 10;
            }
            for (uint32_t s = digits + 2; s < 11; s++) {
                vga_putchar(' ', 0x07);
            }

            /* Cetak status proteksi berkas */
            if (ramdisk[i].is_protected) {
                vga_print("[PROTECTED]\n", 0x0C);
            } else {
                vga_print("[PUBLIC]\n", 0x0A);
            }
        }
    }

    if (count == 0) {
        vga_print("  (RAMDisk kosong)\n", 0x08);
    } else {
        vga_print("  ----------------------------------------------------------\n", 0x08);
        vga_print("  Total: ", 0x07);
        vfs_print_uint((uint32_t)count, 0x0A);
        vga_print(" / ", 0x07);
        vfs_print_uint(MAX_FILES, 0x07);
        vga_print(" slot berkas terpakai.\n", 0x07);
    }
}
