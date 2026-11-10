#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_FILES    16
#define MAX_FILENAME 32
#define MAX_FILESIZE 2048

/* Kode status kembalian operasi VFS */
#define VFS_OK             0
#define VFS_ERR_INVALID   -1
#define VFS_ERR_NOT_FOUND -2
#define VFS_ERR_EXISTS    -3
#define VFS_ERR_FULL      -4
#define VFS_ERR_PROTECTED -5

typedef struct {
    char name[MAX_FILENAME];
    char data[MAX_FILESIZE];
    uint32_t size;
    uint8_t in_use;
    uint8_t is_protected; // 1 = Hanya ROLE_ADMIN / Kernel yang boleh ubah & hapus
} ramdisk_file_t;

void vfs_init(void); // Inisialisasi RAMDisk + buat words.txt & scores.txt bawaan
int vfs_create(const char* name, const char* content, uint8_t is_protected);
int vfs_read(const char* name, char* out_buffer);
int vfs_update(const char* name, const char* new_content);
int vfs_kernel_update(const char* name, const char* new_content);
int vfs_delete(const char* name);
void vfs_list_files(void); // Untuk perintah CLI: ls

#endif
