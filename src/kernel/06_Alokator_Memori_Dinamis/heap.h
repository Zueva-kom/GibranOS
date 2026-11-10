#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

#define HEAP_SIZE 65536 // 64 KB ruang Heap Kernel

typedef struct heap_block {
    size_t             size; // Ukuran data blok (dalam byte)
    uint8_t            free; // 1 = Kosong, 0 = Sedang dipakai
    struct heap_block* next; // Pointer ke blok berikutnya
} heap_block_t;

void  heap_init(void);
void* kmalloc(size_t size);
void  kfree(void* ptr);
size_t heap_get_free_bytes(void);

#endif