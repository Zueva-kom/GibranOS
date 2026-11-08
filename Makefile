
ASM = nasm
CC  = clang
LD  = ld.lld

CFLAGS  = -target i386-pc-none-elf -ffreestanding -O2 -Wall -Wextra -Isrc
LDFLAGS = -m elf_i386 -T src/boot/linker.ld -nostdlib

all: build/kernel.bin

build/boot.o: src/boot/boot.s
	$(ASM) -f elf32 src/boot/boot.s -o build/boot.o

build/main.o: src/kernel/main.c src/drivers/keyboard.h
	$(CC) $(CFLAGS) -c src/kernel/main.c -o build/main.o

build/keyboard.o: src/drivers/keyboard.c src/drivers/keyboard.h
	$(CC) $(CFLAGS) -c src/drivers/keyboard.c -o build/keyboard.o

build/process.o: src/kernel/process/process.c src/kernel/process/process.h
	$(CC) $(CFLAGS) -c src/kernel/process/process.c -o build/process.o

build/kernel.bin: build/boot.o build/main.o build/keyboard.o build/process.o
	$(LD) $(LDFLAGS) build/boot.o build/main.o build/keyboard.o build/process.o -o build/kernel.bin

run: build/kernel.bin
	qemu-system-i386 -kernel build/kernel.bin

clean:
	rm -f build/*.o build/*.bin
