from pathlib import Path

p = Path("Makefile")
s = p.read_text()

if "build/timer.o:" not in s:
    anchor = "build/idt.o: src/arch/i386/idt.c src/arch/i386/idt.h"
    rule = """build/timer.o: src/arch/i386/timer.c src/arch/i386/timer.h src/arch/i386/idt.h src/drivers/io.h
>$(CC) $(CFLAGS) -c src/arch/i386/timer.c -o build/timer.o

build/timer_irq.o: src/arch/i386/timer_irq.s
>$(ASM) -f elf32 src/arch/i386/timer_irq.s -o build/timer_irq.o

"""
    if anchor not in s:
        raise SystemExit("Rule idt.o tidak ditemukan. Makefile tidak diubah.")
    s = s.replace(anchor, rule + anchor)

old = "build/idt.o build/vfs.o"
new = "build/idt.o build/timer.o build/timer_irq.o build/vfs.o"

if old in s:
    s = s.replace(old, new)

p.write_text(s)

print("Makefile berhasil diperbarui.")
