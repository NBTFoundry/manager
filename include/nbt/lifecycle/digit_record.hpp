// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LIFECYCLE_DIGIT_RECORD_HPP
#define NBT_LIFECYCLE_DIGIT_RECORD_HPP

#include "nbt/fs.h"

namespace nbt {
namespace lifecycle {

bool readDigitRecord(nbt_fs *fs, const char *app, nbt_fs_path_kind kind, const char *prefix,
                     unsigned max, unsigned *out);
int writeDigitRecord(nbt_fs *fs, nbt_fs_path_kind next, nbt_fs_path_kind target, const char *app,
                     const char *prefix, unsigned value);

} // namespace lifecycle
} // namespace nbt

#endif
