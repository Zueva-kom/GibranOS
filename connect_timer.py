from pathlib import Path

p = Path("src/kernel/main.c")

# Baca sebagai bytes agar karakter yang tidak dikenal tetap aman.
data = p.read_bytes()

include_old = b'#include "arch/i386/idt.h"'
include_new = (
    b'#include "arch/i386/idt.h"\n'
    b'#include "arch/i386/timer.h"'
)

init_old = b'    idt_init();'
init_new = b'    idt_init();\n    timer_init();'

if b'#include "arch/i386/timer.h"' not in data:
    if include_old not in data:
        raise SystemExit("Include idt.h tidak ditemukan. File tidak diubah.")
    data = data.replace(include_old, include_new, 1)

if b'    timer_init();' not in data:
    if init_old not in data:
        raise SystemExit("Pemanggilan idt_init() tidak ditemukan. File tidak diubah.")
    data = data.replace(init_old, init_new, 1)

p.write_bytes(data)

print("Timer berhasil dihubungkan ke main.c.")
