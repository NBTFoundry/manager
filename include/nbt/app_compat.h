// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_COMPAT_H
#define NBT_APP_COMPAT_H

#include "nbt/fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Whether this manager runs an installed application: its record's bridge
 * declaration against nbt_bridge_current(), the versions and capabilities this
 * manager implements, and the oldest version it still runs. An incompatible
 * application is disabled, never started, until an update makes it compatible
 * (docs/app-bridge.md, "Versions"). Returns 0 when compatible, 1 when not (the
 * reason, when given, says why), -1 when its record cannot be read. */
int nbt_app_compatible(nbt_fs *fs, const char *app, char *reason, unsigned size);

#ifdef __cplusplus
}
#endif
#endif
