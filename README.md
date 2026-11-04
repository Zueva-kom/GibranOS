# GibranOS — Mini-OS Kernel (x86 32-bit)

**GibranOS** adalah sistem operasi *bare-metal* sederhana berbasis arsitektur **x86 (32-bit)** yang dikembangkan menggunakan bahasa **Assembly (NASM)** dan **C (Freestanding)** untuk memenuhi Tugas Besar Sistem Operasi. Kernel ini mendukung standar **Multiboot** dan dijalankan menggunakan emulator **QEMU**.

---

## 1. Prasyarat & Persiapan Lingkungan (Toolchain Setup)

Sebelum melakukan kompilasi dan menjalankan kernel, pastikan seluruh anggota kelompok telah menginstal perangkat lunak berikut:

1. **Git** — Untuk manajemen versi dan *cloning* repositori.
2. **NASM** (*Netwide Assembler*) — Untuk mengompilasi kode *bootstrapping* Assembly (`boot.s`).
3. **Clang & LLD (`ld.lld`)** — *Cross-compiler* C dan *linker* LLVM untuk target `i386-pc-none-elf`.
4. **GNU Make** — Untuk menjalankan otomatisasi *build* melalui `Makefile`.
5. **QEMU (`qemu-system-i386`)** — Emulator perangkat keras x86 untuk menjalankan kernel.

### A. Instalasi di Windows (Pilih salah satu: MSYS2 / Winget / Scoop)
Menggunakan **Winget** (PowerShell / Command Prompt):
```powershell
winget install Git.Git
winget install NASM.NASM
winget install LLVM.LLVM
winget install GnuWin32.Make
winget install SoftwareFreedomConservancy.QEMU
```
> **Penting (Windows):** Pastikan lokasi instalasi (`C:\Program Files\LLVM\bin`, `C:\Program Files\NASM`, `C:\Program Files\qemu`, dan lokasi `make.exe`) sudah ditambahkan ke dalam **Environment Variables (`PATH`)** sistem Anda, lalu *restart* terminal/VS Code.

### B. Instalasi di Linux (Ubuntu / Debian / WSL2)
```bash
sudo apt update
sudo apt install -y git build-essential nasm clang lld qemu-system-x86
```

---

## 2. Cara Clone, Build, dan Menjalankan Proyek

Ikuti langkah-langkah berikut agar seluruh anggota kelompok dapat menjalankan proyek di perangkat masing-masing:

### 1. Clone Repositori dari GitHub
```bash
git clone https://github.com/Zueva-kom/GibranOS.git
cd GibranOS
```

### 2. Pastikan Direktori `build/` Tersedia
Folder `build/` diabaikan oleh Git (`.gitignore`). Buat folder `build/` terlebih dahulu jika belum ada:
```bash
mkdir build
```

### 3. Kompilasi Kernel (*Build*)
Jalankan perintah berikut untuk mengompilasi kode Assembly dan C menjadi binary kernel (`build/kernel.bin`):
```bash
make
```

### 4. Menjalankan Kernel di QEMU (*Run*)
```bash
make run
```
Atau jalankan secara manual:
```bash
qemu-system-i386 -kernel build/kernel.bin
```

### 5. Membersihkan Hasil Build (*Clean*)
- **Linux / Git Bash / MSYS2:**
  ```bash
  make clean
  ```
- **Windows PowerShell:**
  ```powershell
  Remove-Item build\*.o, build\*.bin -ErrorAction SilentlyContinue
  ```

---

## 3. Struktur Proyek

```text
GibranOS/
├── build/                    # Output hasil kompilasi (*.o dan kernel.bin)
├── docs/                     # Dokumentasi laporan & catatan penggunaan AI
│   └── ai_usage_log.md       # Log penggunaan Generative AI
├── src/                      # Source Code utama Mini-OS
│   ├── arch/                 # Kode spesifik arsitektur (GDT/IDT/ISR/Timer)
│   ├── boot/                 # Bootstrapping & Linker Script
│   │   ├── boot.s            # Entry point Multiboot 32-bit & alokasi stack awal
│   │   └── linker.ld         # Konfigurasi tata letak memori kernel (load di 1MB)
│   ├── drivers/              # Driver perangkat keras (Keyboard, Serial UART, VGA)
│   ├── kernel/               # Inti Kernel GibranOS
│   │   ├── main.c            # Fungsi utama kernel (kernel_main) & output VGA
│   │   ├── fs/               # Virtual File System (VFS) & RAMDisk (Anggota 1)
│   │   ├── memory/           # Dynamic Memory / Heap Allocator (Anggota 2)
│   │   ├── process/          # PCB/TCB & Preemptive CPU Scheduler (Anggota 3)
│   │   ├── sync/             # Sinkronisasi Mutex/Semaphore & Deadlock (Anggota 4)
│   │   └── security/         # Proteksi memori & kontrol hak akses (Anggota 4)
│   └── shell/                # Antarmuka CLI Shell & parser perintah internal
├── tests/                    # Skenario pengujian modul (OOM, Race Condition, Deadlock)
├── .gitignore                # Daftar file/folder yang diabaikan Git
├── Makefile                  # Konfigurasi sistem build & runner QEMU
└── README.md                 # Dokumentasi setup, struktur, dan troubleshooting
```

---

## 4. Alur Kerja Git Kelompok (Git Workflow)

Agar tidak terjadi konflik pada branch utama (`main`), setiap anggota kelompok wajib bekerja pada *feature branch* masing-masing yang dibuat dari branch `develop`:

```bash
# 1. Pindah ke branch develop dan ambil pembaruan terbaru
git checkout develop
git pull origin develop

# 2. Buat atau pindah ke branch fitur sesuai pembagian tugas
git checkout -b feature/boot-cli               # Anggota 1 (Boot & CLI Shell)
git checkout -b feature/vfs-ramdisk            # Anggota 1 (VFS & RAMDisk)
git checkout -b feature/memory-allocator       # Anggota 2 (Heap & Drivers)
git checkout -b feature/preemptive-scheduler   # Anggota 3 (Process & Scheduler)
git checkout -b feature/sync-deadlock-security # Anggota 4 (Sync, Deadlock, & Security)

# 3. Simpan perubahan & push ke GitHub
git add .
git commit -m "feat(modul): deskripsi perubahan"
git push -u origin feature/<nama-branch-anda>
```
Setelah fitur diuji dan berjalan lancar di QEMU, buat **Pull Request (PR)** ke branch `develop`.

---

## 5. Troubleshooting (Kendala Umum & Solusi)

| No | Pesan Error / Kendala | Penyebab | Solusi |
| :- | :--- | :--- | :--- |
| **1** | `fatal: unable to open output file 'build/boot.o': 'No such file or directory'` | Direktori `build/` belum ada setelah *clone* pertama kali. | Buat folder `build` terlebih dahulu dengan perintah `mkdir build`, lalu jalankan `make` kembali. |
| **2** | `'make' / 'nasm' / 'clang' / 'qemu-system-i386' is not recognized...` | *Toolchain* belum terinstal atau belum masuk ke variabel `PATH` sistem. | Pastikan aplikasi sudah diinstal dan tambahkan folder `bin` masing-masing *tools* ke **System Environment Variables (`PATH`)**, kemudian *restart* terminal. |
| **3** | `make: ld.lld: Command not found` atau `CreateProcess(NULL, ld.lld ...)` | Paket *linker* LLVM (`lld`) belum terinstal terpisah (umum terjadi di Linux) atau tidak ada di `PATH`. | Di Linux jalankan `sudo apt install lld`. Di Windows pastikan `C:\Program Files\LLVM\bin` ada di dalam `PATH`. |
| **4** | `process_begin: CreateProcess(NULL, rm -f build/*.o ...)` saat `make clean` di Windows | Perintah `rm` adalah perintah Unix dan tidak tersedia secara bawaan di `cmd.exe`. | Jalankan `make clean` melalui terminal **Git Bash / MSYS2 / WSL**, atau hapus manual di PowerShell menggunakan `Remove-Item build\*.o, build\*.bin`. |
| **5** | `fatal error: 'memory/heap.h' file not found` saat mengompilasi modul baru | Direktori `src/kernel` belum ditambahkan ke *include path* compiler pada `Makefile`. | Tambahkan flag `-Isrc/kernel` pada variabel `CFLAGS` di `Makefile`, serta daftarkan target `.o` modul baru pada target `build/kernel.bin`. |
| **6** | QEMU berjalan tetapi jendela langsung tertutup atau *boot loop* | Binary kernel tidak memiliki *header* Multiboot yang berada di 8 KB pertama file ELF. | Pastikan bagian `*(.multiboot)` selalu diletakkan paling awal di dalam seksi `.text` pada file `src/boot/linker.ld`. |
