// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_CAR_DATA_RELAY_HPP
#define NBT_GUI_CAR_DATA_RELAY_HPP

#include "nbt/bridge_hold.h"
#include "nbt/cds_policy.h"
#include "nbt/net_policy.h"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace gui {

/* Car data for applications (capability `car-data`, bridge 1.6). While an
 * application granted it runs, its host keeps <dir>/nbt-bridge1-<app>.want,
 * `<host's monotonic ms> <its signed bridge-cds names>`, refreshed every
 * second; the plugin subscribes to the union of the fresh lists and writes
 * each value to <dir>/nbt-bridge1-cds-<name> (include/nbt/bridge_hold.h). */
static const char kCarDataDir[] = "/dev/shmem";
static const unsigned kCarDataRefreshMs = 1000;

/* A host's record for one application, <dir>/nbt-bridge1-<app>.<suffix>:
 * `<host's monotonic ms> <text>`, refreshed every kCarDataRefreshMs while the
 * application runs; fresh while at most NBT_HOLD_FRESH_MS old. */
int appRecordPath(const char *dir, const char *app, const char *suffix, char *out, size_t size);
int appRecordWrite(const char *dir, const char *app, const char *suffix, uint64_t nowMs,
                   const char *text);
/* The text of a fresh record (at most `size` - 1 bytes). */
bool appRecordRead(const char *path, uint64_t nowMs, char *out, size_t size);
bool appRecordEntry(const char *entry, const char *suffix);

/* Network rules (capability `net`): the host's <app>.net record holds the
 * rules it opened; whether a running application has one on wfd0 (the Wi-Fi
 * Direct group then stays on). */
int netRecordWrite(const char *dir, const char *app, uint64_t nowMs, const char *policy);
bool netWfd0InUse(const char *dir, uint64_t nowMs);

int carDataWantPath(const char *dir, const char *app, char *out, size_t size);
int carDataWantWrite(const char *dir, const char *app, uint64_t nowMs, const char *names);
/* The names of a fresh list (at most NBT_HOLD_FRESH_MS old), validated. */
bool carDataWantRead(const char *path, uint64_t nowMs, char names[NBT_CDS_POLICY_MAX + 1]);
/* Whether a directory entry is a host's list: nbt-bridge1-<app>.want. */
bool carDataWantEntry(const char *entry);
int carDataValuePath(const char *dir, const char *name, char *out, size_t size);
/* UTF-16 car data (NUL terminated) to printable ASCII; false when longer than
 * NBT_CDS_VALUE_MAX or not printable ASCII. */
bool carDataAscii(const unsigned short *text, char out[NBT_CDS_VALUE_MAX + 1]);

} // namespace gui
} // namespace nbt

#endif
