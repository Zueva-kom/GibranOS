#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>

typedef enum {
    ROLE_NONE  = 0,
    ROLE_USER  = 1,
    ROLE_ADMIN = 2
} user_role_t;

/* Manajemen Role & Hak Akses */
user_role_t security_get_role(void);
void security_set_cli_read_flag(int is_cli_read);
int security_require_admin(const char* feature_name);

/* Hook otomatis untuk VFS (menggantikan fungsi weak di vfs.c) */
int security_check_access(uint8_t file_protected, int is_write_op);

/* Halaman Login Gateway */
void security_show_login_screen(void);

#endif