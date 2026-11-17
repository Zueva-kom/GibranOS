#ifndef DEADLOCK_H
#define DEADLOCK_H

/*
 * Modul [5] Deadlock GibranOS
 *
 * use_lock_ordering = 0 -> perintah CLI 'deadlock'
 *     Thread GameInput : kunci mutex_score -> lalu minta mutex_vfs
 *     Thread AutoSave  : kunci mutex_vfs   -> lalu minta mutex_score
 *     Urutan berlawanan => circular wait => kedua thread WAITING selamanya.
 *
 * use_lock_ordering = 1 -> perintah CLI 'deadlock_fix'
 *     Kedua thread wajib mengunci dengan urutan global yang sama:
 *     (1) mutex_score -> (2) mutex_vfs. Circular wait tidak mungkin terjadi.
 */
void deadlock_demo_run(int use_lock_ordering);

#endif
