// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_TRANSACTION_H
#define NBT_TRANSACTION_H

#include "nbt/fs.h"
#include "nbt/package.h"

typedef struct {
    unsigned long long sequence;
    unsigned long long highwater;
    char version[NBT_VERSION_MAX + 1];
    char sha256[65];
    nbt_bridge_requirement bridge;
} nbt_installed;

/* Formats old no-bridge records as NBTSTATE1, bridge records as NBTSTATE2. */
int nbt_tx_record_format(const nbt_installed *record, char *text, size_t size);
int nbt_tx_record_parse(char *text, nbt_installed *record);
int nbt_tx_candidate_bridge(nbt_fs *fs, const char *app, nbt_bridge_requirement *out);

/* All calls require the per-app lock; begin/recover also need the exclusive lease. */
int nbt_tx_lock(nbt_fs *fs, const char *app);
/* Serializes new app dependencies against manager staging. Refuses a pending
 * manager activation. Caller closes >=0; -2 is the legacy app-only layout. */
int nbt_tx_manager_lock(nbt_fs *fs);
int nbt_tx_installed(nbt_fs *fs, const char *app, nbt_installed *record);
int nbt_tx_verify(nbt_fs *fs, const char *app, nbt_package *package, char *error,
                  unsigned error_size);
int nbt_tx_begin(nbt_fs *fs, const char *app, const unsigned char approved_hash[32], int recovery,
                 nbt_package *package, char *error, unsigned error_size);
int nbt_tx_confirm(nbt_fs *fs, const char *app, char *error, unsigned error_size);
int nbt_tx_recover(nbt_fs *fs, const char *app, char *error, unsigned error_size);

#endif
