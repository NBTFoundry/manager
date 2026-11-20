// SPDX-License-Identifier: MPL-2.0
#include "nbt/app_compat.h"

#include "nbt/bridge.h"
#include "nbt/transaction.h"

int nbt_app_compatible(nbt_fs *fs, const char *app, char *reason, unsigned size) {
    nbt_installed record;
    nbt_bridge_provider provider;
    if (reason && size) {
        *reason = 0;
    }
    if (!fs || !app || nbt_tx_installed(fs, app, &record)) {
        return -1;
    }
    nbt_bridge_current(&provider);
    return nbt_bridge_check(&record.bridge, &provider, NULL, NULL, reason, size) ? 1 : 0;
}
