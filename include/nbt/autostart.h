// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_AUTOSTART_H
#define NBT_AUTOSTART_H
#include "nbt/fs.h"

#define NBT_AUTOSTART_FAILURE_LIMIT 3u

typedef struct {
    int enabled;
    unsigned failures;
} nbt_autostart;
/* Missing records mean disabled/zero; malformed or unsafe records fail closed. */
int nbt_autostart_load(nbt_fs *fs, const char *app, nbt_autostart *out);
/* Manager-owned preference. Does not clear the boot guard. Takes app tx lock. */
int nbt_autostart_set(nbt_fs *fs, const char *app, int enabled);
/* Manual START (including show) resets the guard. Takes app tx lock. */
int nbt_autostart_clear(nbt_fs *fs, const char *app);
/* Host holds lifetime and tx locks before calling begin: durable pessimistic
 * increment before launch; 0 attempt allowed, 1 disabled/blocked, -1 error. */
int nbt_autostart_begin(nbt_fs *fs, const char *app);
#endif
