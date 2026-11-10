#include "kernel/memory/heap.h"

static uint8_t heap_area[HEAP_SIZE] __attribute__((aligned(16)));
static heap_block_t* free_list = NULL;

void heap_init(void) {
    free_list = (heap_block_t*)heap_area;
    free_list->size = HEAP_SIZE - sizeof(heap_block_t);
    free_list->free = 1;
    free_list->next = NULL;
}

void* kmalloc(size_t size) {
    if (size == 0 || !free_list) return NULL;

    // Ratakan ukuran ke kelipatan 4 byte (alignment 32-bit)
    size = (size + 3) & ~3;

    heap_block_t* curr = free_list;
    while (curr != NULL) {
        if (curr->free && curr->size >= size) {
            // Jika sisa ruang cukup untuk membuat blok baru, pecah blok (split)
            if (curr->size >= size + sizeof(heap_block_t) + 4) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)curr + sizeof(heap_block_t) + size);
                new_block->size = curr->size - size - sizeof(heap_block_t);
                new_block->free = 1;
                new_block->next = curr->next;

                curr->size = size;
                curr->next = new_block;
            }
            curr->free = 0;
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }

    // Penanganan Out of Memory (OOM) secara aman: kembalikan NULL tanpa crash
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->free = 1;

    // Gabungkan blok kosong yang bersebelahan (Coalescing) untuk mencegah fragmentasi
    heap_block_t* curr = free_list;
    while (curr != NULL && curr->next != NULL) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(heap_block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

size_t heap_get_free_bytes(void) {
    size_t total = 0;
    heap_block_t* curr = free_list;
    while (curr != NULL) {
        if (curr->free) total += curr->size;
        curr = curr->next;
    }
    return total;
}