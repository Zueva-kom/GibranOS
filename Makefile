ASM = nasm
CC  = clang
LD  = ld.lld

CFLAGS  = -target i386-pc-none-elf -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T src/boot/linker.ld -nostdlib

all: build/kernel.bin

build/boot.o: src/boot/boot.s
	$(ASM) -f elf32 src/boot/boot.s -o build/boot.o

build/main.o: src/kernel/main.c
	$(CC) $(CFLAGS) -c src/kernel/main.c -o build/main.o

build/kernel.bin: build/boot.o build/main.o
	$(LD) $(LDFLAGS) build/boot.o build/main.o -o build/kernel.bin

run: build/kernel.bin
	qemu-system-i386 -kernel build/kernel.bin

clean:
	rm -f build/*.o build/*.bin