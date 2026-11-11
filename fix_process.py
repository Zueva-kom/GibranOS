from pathlib import Path

header = Path("src/kernel/process/process.h")
source = Path("src/kernel/process/process.c")

h = header.read_text(encoding="utf-8")
c = source.read_text(encoding="utf-8")

prototypes = """
/* Mengambil data proses berdasarkan indeks */
pcb_t* process_get_by_index(uint32_t index);

/* Mengambil jumlah proses yang aktif */
uint32_t process_get_count(void);
"""

if "process_get_by_index" not in h:
    h = h.replace("#endif", prototypes + "\n#endif")
    header.write_text(h, encoding="utf-8")

functions = """
/* Mengambil proses berdasarkan indeks */
pcb_t* process_get_by_index(uint32_t index) {
    if (index >= MAX_PROCESSES) {
        return NULL;
    }

    if (!process_table[index].used) {
        return NULL;
    }

    return &process_table[index];
}

/* Menghitung jumlah proses yang aktif */
uint32_t process_get_count(void) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].used) {
            count++;
        }
    }

    return count;
}
"""

if "process_get_by_index" not in c:
    c += "\n" + functions
    source.write_text(c, encoding="utf-8")

print("Deklarasi dan fungsi Process berhasil ditambahkan.")
