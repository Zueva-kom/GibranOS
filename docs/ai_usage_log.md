# Log Penggunaan Generative AI — GibranOS (Mini-OS Kernel x86 32-bit)

Dokumen ini mencatat riwayat pemanfaatan **Generative AI (Google Antigravity)** selama pengembangan proyek Tugas Besar Sistem Operasi (**GibranOS**), mencakup ringkasan *prompt*, solusi teknis yang dihasilkan, proses evaluasi/koreksi oleh anggota kelompok, serta berkas kode sumber yang dimodifikasi.

---

## 1. Ringkasan Sesi Pengembangan: Multi-Window Game, Heap Memory Monitor, & Integrasi Preemptive Round Robin

- **Tanggal:** 17 November 2026
- **Modul Terkait:**
  - Modul 02: Pengelolaan Proses & Thread (`src/kernel/02_Pengelolaan_Proses_Thread/`)
  - Modul 03: Penjadwalan CPU Preemptive Round Robin (`src/kernel/03_Penjadwalan_CPU_Preemptive/`)
  - Modul 04: Sinkronisasi Mutex (`src/kernel/04_Sinkronisasi/`)
  - Modul 06: Alokator Memori Dinamis / Heap (`src/kernel/06_Alokator_Memori_Dinamis/`)
  - Modul Aplikasi: Multi-Window Game Typing Defense (`src/kernel/game/`)

---

## 2. Tabel Riwayat Prompt, Kontribusi AI, dan Verifikasi Mahasiswa

| No | Ringkasan Prompt / Permintaan | Hasil / Solusi Teknis dari AI | Evaluasi & Penyempurnaan oleh Mahasiswa | Berkas yang Diubah |
| :-: | :--- | :--- | :--- | :--- |
| **1** | Membuat fitur *Multi-Windows* saat memuat dan menjalankan game sehingga perubahan penggunaan memori dapat terlihat secara langsung. | Merancang antarmuka *Split-Screen Multi-Window TUI* pada buffer VGA `80x25` (`0xB8000`) dan mengubah struktur data game dari array statis menjadi alokasi dinamis di Heap menggunakan `kmalloc()` dan `kfree()`. Menambahkan fungsi `heap_get_head()` untuk menginspeksi *linked-list* blok `heap_block_t` secara langsung. | Memverifikasi arti indikator `Delta Game` (selisih bersih memori Heap saat game berjalan terhadap kondisi awal sebelum game dimuat) serta menguji alokasi bertahap saat *loading* (`Session`, `VFSBuf`, `WordPool`, `SpriteFX`) dan saat bermain (`Monster`, `KillLog`, serta simulasi beban `+4KB` via `PGUP`/`PGDN`). | `src/kernel/game/game.c`<br>`src/kernel/06_Alokator_Memori_Dinamis/heap.h`<br>`src/kernel/06_Alokator_Memori_Dinamis/heap.c`<br>`src/kernel/main.c`<br>`Makefile` |
| **2** | Mengevaluasi kesesuaian seluruh fitur kernel terhadap 8 syarat minimum Tugas Besar Mini-OS serta menanyakan cara kerja implementasi *Round Robin*. | Memetakan status implementasi ke-8 modul dan menjelaskan alur kerja *Preemptive Round Robin* mulai dari interupsi perangkat keras PIT IRQ0 (100 Hz), penyimpanan konteks register di `timer_irq.s`, hingga pergantian `ESP` antar-TCB di `scheduler_tick()`. | Mengidentifikasi bahwa jalannya game dalam satu loop tunggal di thread `Shell` dan thread pekerja `WorkerA`/`WorkerB` yang hanya menambah variabel kosong terasa seperti *gimmick*, sehingga meminta agar *Round Robin* benar-benar diterapkan secara fungsional di dalam game. | Evaluasi arsitektur & perencanaan integrasi |
| **3** | Mengintegrasikan *Preemptive Round Robin Scheduler* dan *Multi-Threading* secara nyata ke dalam *Multi-Window Game*. | Memecah eksekusi game menjadi 3 kernel thread paralel (`Shell`, `MonsterAI`, dan `HeapMon`) yang dilindungi oleh `mutex_t`. Menambahkan fungsi `process_terminate()` untuk transisi status `PROCESS_TERMINATED`, serta memperbaiki `pit_handler()` di `timer.c` dan `mutex_unlock()` di `mutex.c` agar thread berstatus `PROCESS_WAITING` tersimpan nilai `ESP`-nya dan dapat dibangunkan kembali ke `PROCESS_READY`. | Menguji eksekusi di QEMU dan menemukan anomali bahwa angka tick pada UI melompat antara `5~7 tick` padahal quantum Round Robin diatur sebesar `5 tick` (`SCHEDULER_QUANTUM = 5`). | `src/kernel/02_Pengelolaan_Proses_Thread/process.h`<br>`src/kernel/02_Pengelolaan_Proses_Thread/process.c`<br>`src/kernel/03_Penjadwalan_CPU_Preemptive/scheduler.h`<br>`src/kernel/03_Penjadwalan_CPU_Preemptive/scheduler.c`<br>`src/arch/i386/timer.c`<br>`src/kernel/04_Sinkronisasi/mutex.c`<br>`src/kernel/game/game.c` |
| **4** | Memperbaiki perhitungan tick pada UI yang berubah-ubah antara `5~7 tick` agar sesuai dengan quantum `5 tick`. | Menemukan *bug double-increment* pada `scheduler_tick()` di mana `total_ticks++` ditambahkan saat thread sedang berjalan dan ditambahkan lagi (`next->total_ticks++`) saat pergantian thread baru, sehingga 1 quantum (5 tick) terhitung 6 tick. Menghapus penambahan ganda tersebut dan menampilkan akumulasi tick berdasarkan kelipatan quantum (`switches * SCHEDULER_QUANTUM`). | Menguji kembali di QEMU dan mengamati perilaku saat tombol Pause (`[TAB]`) ditekan, lalu mempertanyakan mengapa penggunaan variabel pembeku tampilan masih terasa seperti *gimmick* serta bagaimana memisahkan tugas tiap window secara murni lewat *Round Robin*. | `src/kernel/03_Penjadwalan_CPU_Preemptive/scheduler.c`<br>`src/kernel/game/game.c` |
| **5** | Menghilangkan variabel pembeku buatan (*gimmick*) dan memastikan setiap *Window* benar-benar digambar oleh thread masing-masing saat dijadwalkan oleh *Round Robin*. | Memisahkan tanggung jawab *rendering* memori VGA per wilayah layar:<br>• **Thread `Shell`** hanya menggambar kolom `0..48` (`[WIN 1: GAME]`) melalui `arena_present_rect(1, 0, 23, 48)`.<br>• **Thread `HeapMon`** hanya menggambar kolom `49..79` (`[WIN 2: HEAP]`, `[WIN 3: ROUND ROBIN]`, `[WIN 4: LOG]`) melalui `arena_present_rect(1, 49, 23, 79)` setiap kali memperoleh giliran CPU dari Round Robin.<br>• **Thread `MonsterAI`** dihentikan/dilanjutkan murni melalui perubahan status TCB kernel (`PROCESS_WAITING` $\leftrightarrow$ `PROCESS_READY`) tanpa variabel `if (paused)` buatan. | Memvalidasi kompilasi (`make`) tanpa *warning* maupun *error*, serta memverifikasi bahwa saat `MonsterAI` berstatus `PROCESS_WAITING`, fungsi `scheduler_pick_next()` di kernel otomatis melewati thread tersebut sehingga monster berhenti bergerak secara alami. | `src/kernel/game/game.c` |

---

## 3. Rincian Perubahan Teknis Utama

### A. Pemantauan Memori Dinamis pada Multi-Window (`heap.c` & `game.c`)
1. **Inspeksi Linked-List Heap (`heap_get_head`):**
   Membuka akses baca ke pointer awal `free_list` pada alokator *First-Fit* 64 KB agar **Window 2 (`[WIN 2: HEAP MONITOR]`)** dapat menampilkan jumlah blok terpakai/kosong, persentase beban, dan tabel peta blok beserta pemilik alokasinya (`(Session)`, `(VFSBuf)`, `(WordPool)`, `(SpriteFX)`, `(Monster)`, `(KillLog)`, `(Uji4KB)`).
2. **Alokasi Bertahap & Bebas Kebocoran Memori (*Zero Memory Leak*):**
   - Saat game dibuka, keempat struktur utama dialokasikan secara bertahap dengan `kmalloc()` sehingga kenaikan memori terlihat jelas pada **Window 2** dan **Window 4 (`[WIN 4: LOG MEM & THREAD]`)**.
   - Setiap kali monster baru muncul atau dikalahkan, memori dialokasikan/dibebaskan secara dinamis di Heap.
   - Saat keluar dari game (`[ESC]`), seluruh blok memori dibebaskan dengan `kfree()` sehingga `Delta` kembali ke `+0B`.

### B. Penjadwalan Preemptive Round Robin & Multi-Threading Nyata (`scheduler.c`, `process.c`, `timer.c`, `mutex.c`)
1. **Pemisahan Eksekusi per Window / Tugas:**
   - **`Shell` Thread:** Mengelola input keyboard pengguna dan merender area kiri layar VGA (`[WIN 1: GAME TYPING DEFENSE]`).
   - **`MonsterAI` Thread:** Mengelola waktu mundur permainan dan pergerakan turun monster setiap 100 tick PIT (1 detik).
   - **`HeapMon` Thread:** Memindai *linked-list* Heap dan merender area kanan layar VGA (`[WIN 2]`, `[WIN 3]`, `[WIN 4]`) setiap kali mendapatkan jatah quantum 5 tick.
2. **Perbaikan Context Switch & Sinkronisasi Mutex:**
   - Pada `pit_handler()` (`timer.c`), nilai `current->esp` kini selalu disimpan selama status thread bukan `PROCESS_TERMINATED` (termasuk saat thread baru saja beralih ke `PROCESS_WAITING`).
   - Pada `mutex_lock()` dan `mutex_unlock()` (`mutex.c`), thread yang gagal memperoleh kunci ditandai dengan `waiting_on = (void*)m` dan diubah ke `PROCESS_WAITING`, lalu dibangunkan kembali menjadi `PROCESS_READY` saat pemilik kunci memanggil `mutex_unlock()`.
   - Penambahan fungsi `process_terminate()` (`process.c`) untuk mengubah status PCB/TCB thread game menjadi `PROCESS_TERMINATED` saat keluar dari game (dapat dibuktikan melalui perintah CLI `ps`), sekaligus mendukung penggunaan ulang slot TCB yang sudah `TERMINATED` ketika game dijalankan kembali.
