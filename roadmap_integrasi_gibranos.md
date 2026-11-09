# Roadmap Lanjutan & Integrasi Game Key-Typing di GibranOS

Dokumen ini menyatukan **Ketentuan Wajib Tugas Besar Mini-OS (8 Syarat Minimum)** dengan rencana implementasi **Game Key-Typing** di atas kernel **GibranOS**. Dokumen ini berfungsi sebagai acuan teknis lanjutan setelah tahap *bootstrapping* dan CLI dasar selesai.

---

## Status Saat Ini (Progress Minggu 1)
* [x] Kernel GibranOS berhasil *booting* di QEMU.
* [x] Menampilkan logo ASCII & *system info* ala `fastfetch` (`GIBRANOS`).
* [x] Antarmuka CLI dasar berjalan dan menerima perintah (`help`, `echo`, `shutdown`).

---

## 1. Pemetaan 8 Ketentuan Wajib vs Fitur Game & CLI Pengujian

Agar seluruh syarat minimum pada pedoman penilaian terpenuhi sempurna saat demonstrasi langsung, **Game Key-Typing** akan berperan sebagai aplikasi utama yang mendemonstrasikan kerja sama antar-modul, sementara **perintah CLI khusus (`test_*`)** disediakan untuk membuktikan skenario pengujian spesifik di depan dosen penguji.

| No | Ketentuan Minimum | Status | Implementasi di Game Key-Typing | Perintah CLI Pengujian (Wajib untuk Demo) |
| :- | :--- | :--- | :--- | :--- |
| **1** | **Bootstrapping & CLI Kernel** | **Selesai** | Peluncuran game melalui shell (`game start`). | `help`, `echo`, `clear`, `shutdown`, `game` |
| **2** | **Pengelolaan Proses & Thread** | **Belum** | Game menjalankan minimal 2 thread (*Thread Spawner/Timer* & *Thread Input/UI*). | `ps` (lihat status TCB), `test_thread` (demo transisi *New, Ready, Running, Waiting, Terminated*) |
| **3** | **Penjadwalan CPU Preemptive** | **Belum** | *Context switching* otomatis berbasis *Timer Interrupt* (PIT IRQ0) untuk menggerakkan waktu game. | Berjalan otomatis saat `game` atau `test_thread` dieksekusi |
| **4** | **Sinkronisasi (Mutex/Semaphore)** | **Belum** | Melindungi variabel skor bersama (*shared score*) dan buffer layar saat game berjalan. | `test_race` (tanpa mutex) vs `test_sync` (dengan mutex) pada *shared counter* |
| **5** | **Penanganan Deadlock** | **Belum** | Game menggunakan urutan penguncian (*lock ordering*) yang konsisten agar bebas *deadlock*. | `test_deadlock` (demo 2 thread terkunci silang) & `test_deadlock_fix` (solusi urutan kunci) |
| **6** | **Alokator Memori Dinamis** | **Belum** | Mengalokasikan memori (`kmalloc`) untuk objek kata/skor dan melepasnya (`kfree`) saat selesai. | `meminfo` (kapasitas *heap*), `test_mem` (demo alokasi, *free*, & penanganan *Out of Memory* aman) |
| **7** | **Virtual File System (VFS)** | **Belum** | Memuat daftar kata soal dari `words.txt` dan menyimpan rekor skor ke `scores.txt` di RAMDisk. | `ls`, `create <file> <isi>`, `read <file>`, `update <file> <isi>`, `delete <file>` |
| **8** | **Proteksi & Hak Akses** | **Belum** | Berkas sistem (`words.txt` & `scores.txt`) diproteksi agar tidak bisa dihapus/diubah sembarangan oleh `guest`. | `whoami`, `role <admin/guest>`, uji tolak hapus/ubah berkas terproteksi saat mode `guest` |

---

## 2. Kontrak Antarmuka Antar-Modul (Header Files `.h`)

Sebelum masing-masing anggota menulis kode implementasi (`.c`) di *branch* masing-masing, buat dan sepakati *header files* berikut di branch `develop` agar tidak terjadi konflik saat *merge*:

### A. Modul Memori (`src/kernel/memory/heap.h`) — *Anggota 2*
```c
#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void heap_print_info(void); // Untuk perintah CLI: meminfo
void heap_test_oom(void);   // Untuk perintah CLI: test_mem

#endif
```

### B. Modul Proses & Preemptive Scheduler (`src/kernel/process/scheduler.h`) — *Anggota 3*
```c
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

typedef enum {
    THREAD_NEW,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_WAITING,
    THREAD_TERMINATED
} thread_state_t;

typedef struct tcb {
    uint32_t tid;
    char name[32];
    uint32_t esp;              // Stack pointer yang disimpan saat context switch
    thread_state_t state;
    uint32_t sleep_ticks;
    struct tcb* next;
} tcb_t;

void scheduler_init(void);
int thread_create(void (*entry_point)(void), const char* name);
void thread_sleep(uint32_t ticks);
void thread_exit(void);
void scheduler_print_threads(void); // Untuk perintah CLI: ps

#endif
```

### C. Modul Sinkronisasi & Proteksi (`src/kernel/sync/mutex.h` & `security.h`) — *Anggota 4*
```c
#ifndef MUTEX_H
#define MUTEX_H

typedef struct {
    volatile int locked;
    int owner_tid;
} mutex_t;

void mutex_init(mutex_t* m);
void mutex_lock(mutex_t* m);
void mutex_unlock(mutex_t* m);

// Fungsi demo pengujian Bab III
void sync_test_race_condition(void); // CLI: test_race
void sync_test_with_mutex(void);     // CLI: test_sync
void sync_test_deadlock(int fix);    // CLI: test_deadlock

#endif
```

```c
#ifndef SECURITY_H
#define SECURITY_H

typedef enum {
    ROLE_GUEST = 0,
    ROLE_ADMIN = 1
} user_role_t;

void security_set_role(user_role_t role);
user_role_t security_get_role(void);
int security_check_access(uint8_t file_protected, int is_write_op);

#endif
```

### D. Modul Virtual File System (`src/kernel/fs/vfs.h`) — *Anggota 1*
```c
#ifndef VFS_H
#define VFS_H

#include <stdint.h>

#define MAX_FILES 16
#define MAX_FILENAME 32
#define MAX_FILESIZE 512

typedef struct {
    char name[MAX_FILENAME];
    char data[MAX_FILESIZE];
    uint32_t size;
    uint8_t in_use;
    uint8_t is_protected; // 1 = Hanya ROLE_ADMIN / Kernel yang boleh ubah & hapus
} ramdisk_file_t;

void vfs_init(void); // Inisialisasi RAMDisk + buat words.txt & scores.txt bawaan
int vfs_create(const char* name, const char* content, uint8_t is_protected);
int vfs_read(const char* name, char* out_buffer);
int vfs_update(const char* name, const char* new_content);
int vfs_delete(const char* name);
void vfs_list_files(void); // Untuk perintah CLI: ls

#endif
```

---

## 3. Rincian Tugas Selanjutnya per Anggota

### **Anggota 1: VFS RAMDisk, Integrasi Perintah CLI, & UI Game**
* **Tugas Selanjutnya (Karena Boot & CLI dasar sudah selesai):**
  1. Membuat modul **VFS berbasis RAMDisk** (`src/kernel/fs/vfs.c`) untuk menyimpan berkas di dalam memori RAM.
  2. Mengisi RAMDisk saat *booting* awal dengan 2 berkas bawaan: `words.txt` (daftar kata untuk *game typing*) dan `scores.txt` (penyimpan skor tertinggi).
  3. Menambahkan perintah CLI CRUD berkas (`ls`, `create`, `read`, `update`, `delete`) dan peluncur game (`game`).
  4. Merancang tampilan visual (*UI Arena*) Game Key-Typing menggunakan *buffer* layar VGA (`0xB8000`) yang menampilkan kata target, input ketikan pemain, waktu tersisa, dan skor.

### **Anggota 2: Dynamic Heap Allocator & Keyboard Driver Non-Blocking**
* **Tugas Selanjutnya:**
  1. Mengimplementasikan **Heap Allocator** (`kmalloc` dan `kfree`) menggunakan *First-Fit Linked List* di atas blok array statis kernel (misal $64\text{ KB}$).
  2. Menangani skenario **Out of Memory (OOM)**: jika alokasi melebihi sisa kapasitas *heap*, kembalikan `NULL` dan tampilkan pesan peringatan tanpa membuat kernel *crash/reboot*.
  3. Mengembangkan *Keyboard Driver* berbasis *interrupt* (atau *non-blocking buffer*) agar saat pemain mengetik huruf di dalam game, proses pembacaan tombol tidak menghentikan hitung mundur waktu.
  4. Mengalokasikan memori dinamis untuk objek sesi permainan dan daftar riwayat skor.

### **Anggota 3: Timer Interrupt (PIT), Context Switching, & Preemptive Scheduler**
* **Tugas Selanjutnya:**
  1. Mengonfigurasi **Programmable Interval Timer (PIT IRQ0)** agar memicu *interrupt* secara berkala (misal $100\text{ Hz}$) dan membuat struktur *Interrupt Descriptor Table* (IDT).
  2. Mengimplementasikan rutin *low-level context switching* (Assembly + C) untuk menyimpan dan memulihkan register CPU antar-thread secara *Round-Robin*.
  3. Mengelola kelima status thread (*New, Ready, Running, Waiting, Terminated*) dan menyediakan fungsi `thread_sleep(ticks)` untuk mengatur kecepatan *looping* waktu di dalam game.
  4. Membuat skenario perintah `test_thread` dan `ps` untuk menampilkan bukti perpindahan status thread di terminal.

### **Anggota 4: Sinkronisasi (Mutex), Skenario Deadlock, & Kontrol Hak Akses**
* **Tugas Selanjutnya:**
  1. Mengimplementasikan struktur **Mutex** (`mutex_lock` dan `mutex_unlock`) untuk melindungi variabel skor bersama (*shared score*) dan penulisan layar VGA saat beberapa thread berjalan bersamaan.
  2. Membuat fungsi pengujian `test_race` (2 thread menambah *shared counter* tanpa kunci hingga hasilnya tidak konsisten) dan `test_sync` (hasil konsisten dengan Mutex).
  3. Membuat fungsi pengujian `test_deadlock` (2 thread mengunci `mutex_A` dan `mutex_B` dengan urutan terbalik) beserta solusinya (`test_deadlock_fix` dengan pengurutan kunci).
  4. Mengimplementasikan **Kontrol Hak Akses (Role-Based Access Control sederhana)**: membedakan akses `admin` dan `guest` pada operasi tulis/hapus berkas VFS serta perintah sensitif di CLI.

---

## 4. Spesifikasi Perintah CLI GibranOS (Target Akhir)

Berikut adalah daftar seluruh perintah CLI yang ditargetkan tampil saat pengguna mengetik `help`:

```text
=== DAFTAR PERINTAH GIBRANOS ===
[Umum & Aplikasi]
  help                     : Menampilkan daftar perintah ini
  echo <teks>              : Mencetak teks ke layar
  clear                    : Membersihkan layar terminal
  shutdown                 : Mematikan sistem GibranOS
  game                     : Memulai permainan Key-Typing GibranOS

[File System (RAMDisk VFS)]
  ls                       : Menampilkan daftar berkas dan status proteksinya
  create <nama> <isi>      : Membuat berkas teks baru
  read <nama>              : Membaca isi berkas teks
  update <nama> <isi_baru> : Memperbarui isi berkas teks
  delete <nama>            : Menghapus berkas teks

[Memori, Proses, Sinkronisasi & Keamanan]
  ps                       : Menampilkan daftar thread aktif & statusnya
  meminfo                  : Menampilkan kapasitas memori Heap (terpakai & bebas)
  whoami                   : Menampilkan role pengguna aktif (guest / admin)
  role <guest|admin>       : Mengubah hak akses pengguna untuk pengujian proteksi
  test_thread              : Demo 2 thread & transisi 5 status (New..Terminated)
  test_race                : Demo Race Condition pada shared counter (Tanpa Mutex)
  test_sync                : Demo konsistensi shared counter (Dengan Mutex)
  test_deadlock            : Demo skenario Deadlock 2 mutex & solusi penanganannya
  test_mem                 : Demo alokasi, free, & penanganan Out of Memory (OOM)
```

---

## 5. Checklist Langkah Eksekusi Minggu Ini

1. **Sinkronisasi Repo Git:**
   * Pastikan kode yang sudah bisa *booting*, menampilkan logo `GIBRANOS`, dan menjalankan `help`, `echo`, `shutdown` sudah berada di branch `develop`.
   * Tambahkan file `ROADMAP_INTEGRASI_GIBRANOS.md` ini ke repositori.
2. **Buat Kerangka Folder & Header:**
   * Buat folder `src/kernel/memory/`, `src/kernel/process/`, `src/kernel/sync/`, `src/kernel/fs/`, `src/kernel/security/`, dan `src/kernel/game/`.
   * Masukkan file-file `.h` pada Bagian 2 di atas, lalu *commit* dan *push* ke `develop`.
3. **Pecah Branch Fitur per Anggota:**
   * Anggota 1: `git checkout -b feature/vfs-and-game-ui`
   * Anggota 2: `git checkout -b feature/heap-and-keyboard`
   * Anggota 3: `git checkout -b feature/pit-preemptive-scheduler`
   * Anggota 4: `git checkout -b feature/mutex-deadlock-security`