// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_CAPTURE_PROBE_HPP
#define NBT_STORAGE_CAPTURE_PROBE_HPP

#include "nbt/fs.h"

#include <stddef.h>

namespace nbt {
namespace storage {
namespace capture_io {

/* Runs the installed launcher's --capabilities in HmiMain3D's environment
 * from starter.cfg `cfg` (when it parses): alone, then with a copy of the shim
 * (state/manager/capture-probe.so) after iDrive's preload list and
 * LD_BIND_NOW=1. When iDrive's list cannot bind every symbol outside
 * HmiMain3D, the list is bound on demand and the shim alone with every symbol
 * bound; when it does not load at all, the shim alone. 0 when every run exits
 * 0, with the variant used in detail; otherwise -1 and the reason. Proves the
 * loader accepts the shim, not that it forwards inside HmiMain3D. */
int probeCaptureShim(nbt_fs *fs, const char *cfg, size_t cfgLen, const char *data, size_t len,
                     char *detail, size_t detailSize);

} // namespace capture_io
} // namespace storage
} // namespace nbt

#endif
