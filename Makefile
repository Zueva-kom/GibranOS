
.RECIPEPREFIX := >

ASM = nasm
CC  = clang
LD  = ld.lld

CFLAGS  = -target i386-pc-none-elf -ffreestanding -mno-sse -mno-mmx -O2 -Wall -Wextra -Isrc
LDFLAGS = -m elf_i386 -T src/boot/linker.ld -nostdlib

.PHONY: all run clean

all: build/kernel.bin

build/boot.o: src/boot/boot.s
>$(ASM) -f elf32 src/boot/boot.s -o build/boot.o

build/main.o: src/kernel/main.c src/drivers/keyboard.h src/drivers/serial.h src/kernel/02_Pengelolaan_Proses_Thread/process.h src/kernel/06_Alokator_Memori_Dinamis/heap.h src/kernel/07_Virtual_File_System/vfs.h src/kernel/game/game.h src/arch/i386/gdt.h src/arch/i386/idt.h src/arch/i386/timer.h
>$(CC) $(CFLAGS) -c src/kernel/main.c -o build/main.o

build/keyboard.o: src/drivers/keyboard.c src/drivers/keyboard.h
>$(CC) $(CFLAGS) -c src/drivers/keyboard.c -o build/keyboard.o

build/serial.o: src/drivers/serial.c src/drivers/serial.h src/drivers/io.h
>$(CC) $(CFLAGS) -c src/drivers/serial.c -o build/serial.o

build/process.o: src/kernel/02_Pengelolaan_Proses_Thread/process.c src/kernel/02_Pengelolaan_Proses_Thread/process.h
>$(CC) $(CFLAGS) -c src/kernel/02_Pengelolaan_Proses_Thread/process.c -o build/process.o

build/heap.o: src/kernel/06_Alokator_Memori_Dinamis/heap.c src/kernel/06_Alokator_Memori_Dinamis/heap.h
>$(CC) $(CFLAGS) -c src/kernel/06_Alokator_Memori_Dinamis/heap.c -o build/heap.o

build/mutex.o: src/kernel/04_Sinkronisasi/mutex.c src/kernel/04_Sinkronisasi/mutex.h src/kernel/02_Pengelolaan_Proses_Thread/process.h
>$(CC) $(CFLAGS) -c src/kernel/04_Sinkronisasi/mutex.c -o build/mutex.o

build/gdt.o: src/arch/i386/gdt.c src/arch/i386/gdt.h
>$(CC) $(CFLAGS) -c src/arch/i386/gdt.c -o build/gdt.o

build/gdt_flush.o: src/arch/i386/gdt_flush.s
>$(ASM) -f elf32 src/arch/i386/gdt_flush.s -o build/gdt_flush.o

build/timer.o: src/arch/i386/timer.c src/arch/i386/timer.h src/arch/i386/idt.h src/drivers/io.h
>$(CC) $(CFLAGS) -c src/arch/i386/timer.c -o build/timer.o

build/timer_irq.o: src/arch/i386/timer_irq.s
>$(ASM) -f elf32 src/arch/i386/timer_irq.s -o build/timer_irq.o

build/idt.o: src/arch/i386/idt.c src/arch/i386/idt.h
>$(CC) $(CFLAGS) -c src/arch/i386/idt.c -o build/idt.o

build/vfs.o: src/kernel/07_Virtual_File_System/vfs.c src/kernel/07_Virtual_File_System/vfs.h
>$(CC) $(CFLAGS) -c src/kernel/07_Virtual_File_System/vfs.c -o build/vfs.o

build/game.o: src/kernel/game/game.c src/kernel/game/game.h src/kernel/07_Virtual_File_System/vfs.h src/drivers/keyboard.h
>$(CC) $(CFLAGS) -c src/kernel/game/game.c -o build/game.o

build/kernel.bin: build/boot.o build/main.o build/keyboard.o build/serial.o build/process.o build/heap.o build/mutex.o build/gdt.o build/gdt_flush.o build/idt.o build/timer.o build/timer_irq.o build/vfs.o build/game.o
>$(LD) $(LDFLAGS) build/boot.o build/main.o build/keyboard.o build/serial.o build/process.o build/heap.o build/mutex.o build/gdt.o build/gdt_flush.o build/idt.o build/timer.o build/timer_irq.o build/vfs.o build/game.o -o build/kernel.bin

run: build/kernel.bin
>qemu-system-i386 -kernel build/kernel.bin

clean:
>rm -f build/*.o build/*.bin
