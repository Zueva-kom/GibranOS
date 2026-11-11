# Panduan Pengerjaan & Alur Kerja Tugas Besar Mini-OS

Dokumen ini berisi panduan alur kerja, pembagian tugas kelompok (4 anggota), struktur repositori, dan strategi Git untuk penyelesaian proyek Tugas Besar Mini-OS dalam kurun waktu 1 bulan.

## 1. Pembagian Tugas Kelompok (4 Anggota)

Semua anggota memiliki posisi sejajar dan bertanggung jawab atas integrasi komponen utama, pengujian, serta penyusunan bab laporan terkait.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        MINI-OS KERNEL SYSTEM                           │
├───────────────┬────────────────┬───────────────────────┬───────────────┤
│   ANGGOTA 1   │   ANGGOTA 2    │       ANGGOTA 3       │   ANGGOTA 4   │
│ Boot & Shell  │ Dynamic Memory │  Process & Scheduler  │ Synchronization│
│  & RAMDisk    │  & Drivers     │  (Preemptive Switch)  │   & Security  │
└───────────────┴────────────────┴───────────────────────┴───────────────┘

```

### **Anggota 1: Bootstrapping, CLI Shell, & Virtual File System (VFS)**

* **Tanggung Jawab Modul:**

  * Bootloader & setup arsitektur kernel dasar (menampilkan logo ASCII & banner booting).

  * Antarmuka CLI internal berbasis keyboard input/serial terminal.

  * Virtual File System (VFS) berbasis RAMDisk dengan operasi CRUD (Create, Read, Update, Delete).

* **Tanggung Jawab Dokumen & Laporan:**

  * Menyusun Bab I (Pendahuluan) & Bab II (Sub-bab 1 & 7: Bootstrapping/CLI & VFS).

  * Menyiapkan Lampiran 1 (Link Repositori) & Lampiran 2 (Petunjuk Build/Run QEMU).

### **Anggota 2: Memory Management & Device Drivers**

* **Tanggung Jawab Modul:**

  * Dynamic Memory Allocator (Heap allocator: implementasi fungsi alokasi dan pelepasan memori).

  * Penanganan kondisi *Out of Memory* (OOM) secara aman tanpa crash.

  * Driver perantara hardware dasar (Serial UART / Keyboard Driver / Display Text Mode).

* **Tanggung Jawab Dokumen & Laporan:**

  * Menyusun Bab II (Sub-bab 6: Alokator Memori Dinamis).

  * Dokumentasi skenario & screenshot pengujian alokasi/pelepasan memori untuk Bab III.

### **Anggota 3: Process & CPU Preemptive Scheduler**

* **Tanggung Jawab Modul:**

  * Pengelolaan Proses & Thread (Struktur data TCB/PCB, transisi status: *New, Ready, Running, Waiting, Terminated*).

  * Preemptive Scheduler (Round Robin / Priority Scheduling berbasis Timer Interrupt).

  * Mekanisme low-level *context switching* (penyimpanan & pemulihan register CPU).

* **Tanggung Jawab Dokumen & Laporan:**

  * Menyusun Bab II (Sub-bab 2 & 3: Pengelolaan Thread & Penjadwalan CPU).

  * Dokumentasi skenario & bukti pengujian *context switching* untuk Bab III.

### **Anggota 4: Synchronization, Deadlock, & System Security**

* **Tanggung Jawab Modul:**

  * Mekanisme Sinkronisasi (Implementasi Mutex atau Semaphore).

  * Skenario & Penanganan Deadlock (Demonstrasi *circular wait* & solusi pengurutan kunci).

  * Proteksi & Hak Akses (Pemisahan User/Kernel Mode atau Access Control List sederhana).

* **Tanggung Jawab Dokumen & Laporan:**

  * Menyusun Bab II (Sub-bab 4, 5, & 8: Sinkronisasi, Deadlock, & Proteksi).

  * Menyusun Bab IV (Penutup: Ringkasan, Keterbatasan, & Rekomendasi).

  * Menyiapkan Lampiran 3 (Dokumentasi Penggunaan Gen-AI & Pihak Ketiga) & Lampiran 4 (Tabel Pembagian Tugas).

## 2. Timeline & Milestones Pengerjaan (4 Minggu)

| Minggu | Target Utama | Output / Deliverables | 
| ----- | ----- | ----- | 
| **Minggu 1** | **Setup Environment & Core Primitive** | • Repo Git siap & QEMU bisa *boot* kernel sederhana.  • CLI dasar menampilkan Logo ASCII.  • TCB/PCB struct disiapkan.  • Primitive Mutex dibuat.  • Heap Allocator awal disiapkan. | 
| **Minggu 2** | **Implementasi Fitur Utama Kernel** | • Preemptive Scheduler & *context switching* berfungsi.  • Heap Allocator mendukung alokasi/pelepasan & OOM handling.  • RAMDisk VFS mendukung operasi CRUD berkas teks.  • Mutex lock/unlock diuji pada shared counter. | 
| **Minggu 3** | **Sinkronisasi, Proteksi, & Integrasi Shell** | • Skenario Race Condition & Deadlock demo dibuat.  • Mekanisme Hak Akses / Proteksi memori aktif.  • CLI Shell terintegrasi dengan perintah memanggil fitur VFS, Thread, Memory, & Sync. | 
| **Minggu 4** | **Pengujian Sistem & Penyusunan Laporan** | • Pengujian menyeluruh & ekspor log/screenshot terminal QEMU.  • Penyusunan Laporan LENGKAP (Bab I - IV + Lampiran, 10–15 halaman).  • Simulasi / Geladi bersih demonstrasi langsung. | 

## 3. Struktur Repositori Git

```
GIBRAN OS/
├── src/
│   ├── arch/
│   │   └── i386/
│   │       ├── gdt.c
│   │       ├── gdt.h
│   │       ├── gdt_flush.s
│   │       ├── idt.c
│   │       ├── idt.h
│   │       ├── timer.c
│   │       ├── timer.h
│   │       └── timer_irq.s
│   │
│   ├── boot/
│   │   ├── boot.s
│   │   └── linker.ld
│   │
│   ├── drivers/
│   │   ├── io.h
│   │   ├── keyboard.c
│   │   ├── keyboard.h
│   │   ├── serial.c
│   │   └── serial.h
│   │
│   ├── shell/
│   │   ├── shell.c
│   │   └── shell.h
│   │
│   └── kernel/
│       ├── main.c
│       ├── 01_Bootstrapping_CLI/
│       ├── 02_Pengelolaan_Proses_Thread/
│       │   ├── process.c
│       │   └── process.h
│       ├── 03_Penjadwalan_CPU_Preemptive/
│       │   ├── scheduler.h
│       │   └── scheduler.c       (belum ada)
│       ├── 04_Sinkronisasi/
│       │   ├── mutex.c
│       │   └── mutex.h
│       ├── 05_Deadlock/
│       │   ├── deadlock.c        (belum ada)
│       │   └── deadlock.h        (belum ada)
│       ├── 06_Alokator_Memori_Dinamis/
│       │   ├── heap.c
│       │   └── heap.h
│       ├── 07_Virtual_File_System/
│       │   ├── vfs.c
│       │   └── vfs.h
│       ├── 08_Proteksi_Hak_Akses/
│       │   ├── security.c        (belum ada)
│       │   └── security.h        (belum ada)
│       └── game/
│           ├── game.c
│           └── game.h

```

## 4. Alur Kerja Git (Git Workflow)

Sistem branching menggunakan **Feature Branch Workflow** agar setiap anggota dapat bekerja pada modulnya masing-masing tanpa merusak kode utama.

### **Skema Branching:**

* **`main`**: Berisi kode kernel yang stabil, tidak error, dan siap didemonstrasikan.

* **`develop`**: Branch utama untuk penggabungan (*integration*) fitur-fitur yang sudah diuji.

* **`feature/<nama-modul>`**: Branch spesifik untuk masing-masing anggota.

  * `feature/boot-cli` *(Anggota 1)*

  * `feature/vfs-ramdisk` *(Anggota 1)*

  * `feature/memory-allocator` *(Anggota 2)*

  * `feature/preemptive-scheduler` *(Anggota 3)*

  * `feature/sync-deadlock-security` *(Anggota 4)*

### **Aturan Panduan Git Harian:**

1. **Membuat Feature Branch Baru (dari `develop`):**

   ```
   git checkout develop
   git pull origin develop
   git checkout -b feature/nama-fitur-kalian
   
   ```

2. **Format Commit Message:**
   Gunakan format commit yang rapi agar riwayat pengerjaan mudah dilacak:

   * `feat(shell): tambah perintah CRUD pada CLI`

   * `fix(memory): atasi error OOM pada alokator heap`

   * `docs(report): tambah uraian bab 2 sub-bab sinkronisasi`

3. **Menyinkronkan Branch dengan `develop` Sebelum Push:**
   Untuk mencegah *merge conflict*, jalankan pembaruan dari `develop` secara berkala:

   ```
   git checkout develop
   git pull origin develop
   git checkout feature/nama-fitur-kalian
   git rebase develop
   
   ```

4. **Penggabungan Kode (Pull Request / PR):**

   * Setelah fitur selesai dan berjalan baik di QEMU, lakukan `push` ke remote repositori.

   * Buat **Pull Request (PR)** dari `feature/nama-fitur` menuju `develop`.

   * Minta minimal **1 anggota lain** untuk me-review kode sebelum di-merge ke `develop`.

   * Jangan pernah push langsung ke branch `main`.

## 5. Ringkasan Tugas Penyusunan Laporan (10-15 Halaman)

| Bab / Bagian | Deskripsi Konten | Penanggung Jawab | 
| ----- | ----- | ----- | 
| **Bab I: Pendahuluan** | Deskripsi Mini-OS, fitur utama, bahasa pemrograman, toolchain, & QEMU env. | Anggota 1 | 
| **Bab II: Rancangan & Implementasi** | • Booting & CLI (Sub 1)  • Process & Thread (Sub 2)  • Penjadwalan CPU (Sub 3)  • Sinkronisasi & Race Condition (Sub 4)  • Deadlock (Sub 5)  • Manajemen Memori (Sub 6)  • Sistem Berkas / VFS (Sub 7)  • Proteksi Akses (Sub 8) | • Anggota 1 (Sub 1, 7)  • Anggota 2 (Sub 6)  • Anggota 3 (Sub 2, 3)  • Anggota 4 (Sub 4, 5, 8) | 
| **Bab III: Pengujian & Hasil** | Skenario pengujian, tangkapan layar QEMU, log eksekusi, kendala & solusi. | Semua Anggota (sesuai modulnya) | 
| **Bab IV: Penutup** | Ringkasan hasil implementasi, keterbatasan sistem, & rekomendasi. | Anggota 4 | 
| **Lampiran** | • Link Repo & Petunjuk Build (Lampiran 1 & 2)  • Log Generative AI & Pihak Ke-3 (Lampiran 3)  • Tabel Pembagian Kerja Kelompok (Lampiran 4) | • Anggota 1 (Lampiran 1 & 2)  • Anggota 4 (Lampiran 3 & 4) | 
