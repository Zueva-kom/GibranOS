# Laporan Tugas Besar Sistem Operasi — Bagian Anggota 1
**(Bab I: Pendahuluan, Bab II: Sub-bab 1 & Sub-bab 7, Bab III: Pengujian Modul 1 & 7, serta Lampiran 1 & 2)**

---

## BAB I: PENDAHULUAN

### 1.1 Deskripsi Umum GibranOS
**GibranOS** adalah sistem operasi *bare-metal* sederhana (*Mini-OS*) berarsitektur **x86 32-bit (Protected Mode)** yang dirancang dan diimplementasikan dari awal (*from scratch*) tanpa bergantung pada *standard library* sistem operasi host (`-ffreestanding -nostdlib`). Sistem operasi ini memenuhi spesifikasi **Multiboot v1**, sehingga dapat dimuat langsung ke memori RAM oleh bootloader maupun emulator perangkat keras **QEMU**.

Di atas fondasi kernel tersebut, GibranOS mengintegrasikan delapan komponen inti sistem operasi modern:
1. **Bootstrapping & Antarmuka CLI Kernel** (dengan dukungan layar teks VGA `80x25` + *scrollback history* 200 baris, input Keyboard PS/2, Mouse Scroll, dan Serial UART COM1).
2. **Pengelolaan Proses & Kernel Thread** berbasis struktur *Process Control Block* (PCB) dan *Thread Control Block* (TCB) dengan lima status siklus hidup (*New, Ready, Running, Waiting, Terminated*).
3. **Penjadwalan CPU Preemptive** menggunakan algoritma **Round Robin** yang dipicu oleh interupsi perangkat keras *Programmable Interval Timer* (PIT IRQ0, 100 Hz, *quantum* 5 tick).
4. **Sinkronisasi Antar-Thread** menggunakan mekanisme **Mutex** berbasis operasi atomik.
5. **Skenario & Penanganan Deadlock** menggunakan metode **Lock Ordering** untuk mencegah kondisi *Circular Wait*.
6. **Alokator Memori Dinamis (Kernel Heap)** sebesar 64 KB menggunakan algoritma **First-Fit** dengan fitur *block splitting*, *coalescing*, dan penanganan *Out of Memory* (OOM) secara aman.
7. **Virtual File System (VFS) Berbasis RAMDisk** yang mendukung operasi *Create, Read, Update,* dan *Delete* (CRUD) pada berkas teks di memori RAM.
8. **Proteksi & Kontrol Hak Akses (Role-Based Access Control)** yang memisahkan hak akses **USER** dan **ADMIN** melalui gerbang autentikasi (*Security Gateway Login*) serta tiga tingkat proteksi berkas (`[USER]`, `[PUBLIC]`, dan `[PROTECTED]`).

Sebagai studi kasus integrasi seluruh modul di atas, GibranOS juga menyediakan aplikasi interaktif **Multi-Window Game Typing Defense** yang menjalankan tiga thread kernel paralel (`Shell`, `MonsterAI`, dan `HeapMon`) untuk memvisualisasikan alokasi memori Heap dan pergantian konteks Round Robin secara *real-time*.

### 1.2 Bahasa Pemrograman, Toolchain, dan Lingkungan Eksekusi
Pengembangan GibranOS menggunakan perangkat lunak dan *toolchain* berikut:
* **Bahasa Pemrograman:**
  * **Assembly x86 (NASM):** Digunakan untuk *Multiboot header*, inisialisasi *stack pointer* awal (`src/boot/boot.s`), pemuatan tabel GDT (`src/arch/i386/gdt_flush.s`), serta *Interrupt Service Routine* (ISR) untuk penyimpanan konteks CPU saat *context switching* (`src/arch/i386/timer_irq.s`).
  * **C (C99 Freestanding):** Digunakan untuk seluruh logika inti kernel, driver perangkat keras, manajemen proses/memori/VFS, dan antarmuka CLI (`-target i386-pc-none-elf -ffreestanding -mno-sse -mno-mmx`).
* **Toolchain Kompilasi & Pembuatan Image:**
  * **NASM (*Netwide Assembler*):** Mengompilasi berkas `.s` menjadi objek ELF 32-bit (`-f elf32`).
  * **Clang (LLVM C Compiler):** Mengompilasi berkas sumber `.c` menjadi objek ELF 32-bit tanpa *standard library*.
  * **LLD (`ld.lld`):** Menggabungkan seluruh berkas objek (`.o`) menjadi binary kernel ELF 32-bit (`build/kernel.bin`) sesuai peta memori pada `src/boot/linker.ld`.
  * **GNU Make:** Mengotomatiskan proses *build* dan eksekusi lewat berkas `Makefile`.
* **Lingkungan Emulator:**
  * **QEMU (`qemu-system-i386`):** Menjalankan dan menguji binary kernel `build/kernel.bin` pada mesin virtual x86 lengkap dengan emulasi tampilan VGA (`0xB8000`), pengontrol keyboard/mouse PS/2 (`0x60`/`0x64`), timer PIT (`0x40`/`0x43`), dan port serial UART 16550 COM1 (`0x3F8`).

---

## BAB II: RANCANGAN & IMPLEMENTASI (BAGIAN ANGGOTA 1)

### 2.1 Bootstrapping & Antarmuka CLI Kernel (Sub-bab 1)

#### A. Tahap Bootstrapping (`src/boot/boot.s` & `src/boot/linker.ld`)
Proses *booting* GibranOS dimulai ketika QEMU (atau bootloader Multiboot seperti GRUB) memindai 8 KB pertama dari berkas `build/kernel.bin` untuk mencari tanda tangan **Multiboot v1 Header**:
1. **Multiboot Header (`boot.s`):**
   Mendefinisikan nilai `MAGIC = 0x1BADB002`, `FLAGS = 0x03` (`MBALIGN | MEMINFO`), dan `CHECKSUM = -(MAGIC + FLAGS)` pada seksi `.multiboot` agar bootloader mengenali binary sebagai kernel 32-bit yang sah.
2. **Tata Letak Memori (`linker.ld`):**
   Menempatkan alamat muat awal kernel pada alamat fisik **`1 MB` (`0x00100000`)** untuk menghindari area memori rendah BIOS dan buffer video VGA (`0xA0000–0xFFFFF`). Urutan seksi disusun mulai dari `.text` (diawali `*(.multiboot)`), `.rodata`, `.data`, hingga `.bss`.
3. **Inisialisasi Stack & Lompatan ke C (`_start`):**
   Pada seksi `.bss`, dialokasikan area *stack* awal sebesar **16 KB (`16384 bytes`)** dengan perataan 16-byte (`stack_bottom` hingga `stack_top`). Label `_start` mengarahkan register `esp` ke `stack_top`, lalu memanggil fungsi utama `kernel_main()` di `src/kernel/main.c`.

#### B. Inisialisasi Kernel & Tampilan Layar VGA (`src/kernel/main.c`)
Ketika `kernel_main()` dieksekusi:
1. Interupsi dimatikan sementara (`cli`) untuk menyiapkan struktur dasar CPU: **GDT** (`gdt_init`), **IDT** (`idt_init`), **PIT Timer 100 Hz** (`timer_init`), **Heap Allocator 64 KB** (`heap_init`), **Round Robin Scheduler** (`scheduler_init`), dan **Tabel PCB/TCB** (`process_init`).
2. Kernel membuat tiga thread awal (`WorkerA`, `WorkerB`, dan `Shell`), menginisialisasi driver perangkat keras (`keyboard_init`, `mouse_init`, `vfs_init`, `serial_init`), menampilkan banner ASCII `GIBRANOS`, lalu mengaktifkan interupsi (`sti`) sehingga scheduler memulai eksekusi thread `Shell`.
3. Output visual dirender ke memori **VGA Text Mode Buffer (`0xB8000`)** berukuran `80x25` karakter (2 byte per sel: 1 byte atribut warna dan 1 byte karakter CP437), dilengkapi `history_buffer` sebanyak **200 baris** yang mendukung *scrolling* ke atas/bawah menggunakan tombol `UP`/`DOWN`, `PGUP`/`PGDN`, maupun *scroll wheel* mouse. Setiap karakter yang dicetak ke VGA juga dikirimkan ke port **Serial UART COM1 (`0x3F8`)** melalui `serial_putchar()`.

#### C. Antarmuka Command Line Interface (`src/shell/shell.h` & `src/shell/shell.c`)
Modul CLI Shell diimplementasikan secara modular pada `src/shell/shell.c`:
* **`shell_task()` & `shell_run()`:** Menjalankan layar autentikasi awal (`security_show_login_screen()`), menampilkan prompt sesuai peran aktif (`GibranOS-CLI [ADMIN] $` atau `GibranOS-CLI [USER] $`), dan melakukan pembacaan karakter secara *non-blocking* baik dari **Keyboard PS/2 (`keyboard_poll_char()`)** maupun **Terminal Serial QEMU (`serial_has_data()` / `serial_getchar()`)**.
* **`shell_execute(const char* cmd)`:** Mem-parsing string perintah beserta argumennya menggunakan fungsi bantu `extract_token()`, lalu memanggil rutin modul yang bersangkutan:
  * **Perintah Dasar & Sistem:** `help`, `clear`, `version`, `echo <teks>`, `game`, `logout`, `shutdown`.
  * **Perintah Proses, Memori, Sinkronisasi & Keamanan:** `ps`, `schedtest`, `mutex_test`, `deadlock`, `deadlock_fix`, `meminfo`, `test_mem`, `access_test`.
  * **Perintah Berkas VFS:** `ls`, `create`, `read`, `update`, `delete`.

---

### 2.7 Virtual File System (VFS) Sederhana Berbasis RAMDisk (Sub-bab 7)

#### A. Struktur Data RAMDisk (`src/kernel/07_Virtual_File_System/vfs.h`)
Sistem berkas GibranOS diimplementasikan di dalam memori RAM (*in-memory RAMDisk*) melalui array statis `ramdisk[MAX_FILES]` yang mampu menampung hingga **16 berkas** (`MAX_FILES = 16`), dengan panjang nama maksimal **32 karakter** (`MAX_FILENAME = 32`) dan kapasitas isi hingga **2048 byte per berkas** (`MAX_FILESIZE = 2048`):

```c
typedef struct {
    char name[MAX_FILENAME];
    char data[MAX_FILESIZE];
    uint32_t size;
    uint8_t in_use;
    uint8_t is_protected; /* 0 = [USER], 1 = [PUBLIC], 2 = [PROTECTED] */
} ramdisk_file_t;
```

#### B. Operasi CRUD & Integrasi Hak Akses (`src/kernel/07_Virtual_File_System/vfs.c`)
1. **`vfs_init()`:** Mengosongkan seluruh slot RAMDisk saat booting, kemudian membuat 2 berkas sistem bawaan berstatus `[PROTECTED]`:
   * `words.txt`: Berisi ratusan kosakata bahasa Inggris (3 hingga >9 huruf) sebagai bank soal **Game Typing Defense**.
   * `scores.txt`: Menyimpan rekor skor tertinggi permainan secara persisten selama sesi OS berjalan.
2. **`vfs_create(name, content, is_protected)` (Create):** Memvalidasi nama berkas, memastikan belum ada berkas bernama sama (`VFS_ERR_EXISTS`), mencari slot kosong (`VFS_ERR_FULL` jika penuh), menyalin isi berkas, serta menetapkan level proteksinya.
3. **`vfs_read(name, out_buffer)` (Read):** Mencari indeks berkas berdasarkan nama (`VFS_ERR_NOT_FOUND` jika tidak ada), memeriksa izin baca melalui `security_check_access(file_protected, 0)`, lalu menyalin isi berkas ke `out_buffer`.
4. **`vfs_update(name, new_content)` (Update):** Mencari berkas, memeriksa izin tulis melalui `security_check_access(file_protected, 1)` (menolak perubahan oleh `USER` pada berkas `[PUBLIC]` maupun `[PROTECTED]` milik `ADMIN`), lalu memperbarui isi dan ukuran berkas.
5. **`vfs_delete(name)` (Delete):** Memeriksa izin tulis/hapus melalui `security_check_access()`, lalu mengosongkan kembali slot `ramdisk[idx]` (`in_use = 0`) agar slot dapat digunakan ulang.
6. **`vfs_list_files()` (`ls`):** Menampilkan tabel daftar berkas yang sedang tersimpan di RAMDisk beserta ukuran dalam byte, label status proteksi (`[USER]`, `[PUBLIC]`, `[PROTECTED]`), serta total slot terpakai.

---

## BAB III: PENGUJIAN & HASIL (MODUL 1 & MODUL 7)

### 3.1 Pengujian Bootstrapping & Perintah CLI (Modul 1)
* **Tujuan Pengujian:** Membuktikan bahwa GibranOS berhasil *booting* di QEMU, menampilkan antarmuka login & banner ASCII kernel, serta mengeksekusi minimal 3 perintah CLI buatan kelompok.
* **Langkah Pengujian di QEMU:**
  1. Jalankan perintah `make run` (atau `qemu-system-i386 -kernel build/kernel.bin`).
  2. Pada layar **GibranOS Security Gateway**, masukkan username `admin` dan password `admin123` (atau `user` / `user`).
  3. Setelah banner ASCII `GIBRANOS` tampil, jalankan perintah CLI berikut secara berurutan:
     ```text
     GibranOS-CLI [ADMIN] $ version
     GibranOS v0.1.2 (x86 32-bit Multiboot Kernel)

     GibranOS-CLI [ADMIN] $ echo Halo Dosen Penguji Sistem Operasi!
     Halo Dosen Penguji Sistem Operasi!

     GibranOS-CLI [ADMIN] $ help
     ```
* **Hasil Pengujian:** Kernel berhasil dimuat oleh QEMU pada mode 32-bit Protected Mode, merender banner ASCII dan antarmuka CLI dengan lancar, serta mengeksekusi perintah `version`, `echo`, `help`, `clear`, dan `shutdown` sesuai spesifikasi.

### 3.2 Pengujian Operasi CRUD Virtual File System / RAMDisk (Modul 7)
* **Tujuan Pengujian:** Membuktikan keberhasilan operasi **Create, Read, Update, dan Delete (CRUD)** pada berkas teks di RAMDisk VFS.
* **Langkah Pengujian di QEMU:**
  1. **Lihat Daftar Berkas Awal (`ls`):**
     ```text
     GibranOS-CLI [ADMIN] $ ls
     ```
     *Hasil:* Menampilkan 2 berkas sistem bawaan (`words.txt` dan `scores.txt`).
  2. **Operasi CREATE (`create`):**
     ```text
     GibranOS-CLI [ADMIN] $ create catatan.txt public Ini adalah isi awal berkas catatan kelompok.
     [VFS] Berkas 'catatan.txt' berhasil dibuat [PUBLIC].
     ```
  3. **Operasi READ (`read`):**
     ```text
     GibranOS-CLI [ADMIN] $ read catatan.txt
     --- Isi Berkas: catatan.txt ---
     Ini adalah isi awal berkas catatan kelompok.
     ```
  4. **Operasi UPDATE (`update`):**
     ```text
     GibranOS-CLI [ADMIN] $ update catatan.txt Isi berkas berhasil diperbarui pada pengujian VFS!
     [VFS] Berkas 'catatan.txt' berhasil diperbarui.

     GibranOS-CLI [ADMIN] $ read catatan.txt
     --- Isi Berkas: catatan.txt ---
     Isi berkas berhasil diperbarui pada pengujian VFS!
     ```
  5. **Operasi DELETE (`delete`):**
     ```text
     GibranOS-CLI [ADMIN] $ delete catatan.txt
     [VFS] Berkas 'catatan.txt' berhasil dihapus.

     GibranOS-CLI [ADMIN] $ read catatan.txt
     [VFS] Gagal: Berkas 'catatan.txt' tidak ditemukan.
     ```
* **Hasil Pengujian:** Seluruh operasi CRUD (`create`, `read`, `update`, `delete`) serta `ls` berjalan dengan benar di atas RAMDisk dan terintegrasi langsung dengan kontrol hak akses `ADMIN` / `USER`.

---

## LAMPIRAN 1 & LAMPIRAN 2

### Lampiran 1: Tautan Repositori Kode Sumber
* **URL Repositori GitHub:** `https://github.com/Zueva-kom/GibranOS`

### Lampiran 2: Petunjuk Kompilasi (Build) dan Eksekusi di QEMU
1. **Persiapan Direktori Build:**
   ```bash
   git clone https://github.com/Zueva-kom/GibranOS.git
   cd GibranOS
   mkdir build
   ```
2. **Kompilasi Kernel (`build/kernel.bin`):**
   ```bash
   make
   ```
3. **Menjalankan GibranOS di Emulator QEMU:**
   * Mode standar (Jendela VGA):
     ```bash
     make run
     ```
   * Mode dengan Terminal Serial aktif di konsol host:
     ```bash
     qemu-system-i386 -kernel build/kernel.bin -serial stdio
     ```
4. **Akun Login Bawaan Sistem:**
   * **Akun Administrator (Akses Penuh Menu 1–8):** Username `admin` | Password `admin123`
   * **Akun User Biasa (Akses Terbatas):** Username `user` | Password `user`
