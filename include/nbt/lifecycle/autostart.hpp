// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LIFECYCLE_AUTOSTART_HPP
#define NBT_LIFECYCLE_AUTOSTART_HPP

#include "nbt/autostart.h"
#include "nbt/core/status.hpp"
#include "nbt/storage/locks.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace lifecycle {

class Autostart {
public:
    static core::Result<nbt_autostart> load(const storage::ManagedRoot &root, const char *app);
    static core::Status set(const storage::ManagedRoot &root, const char *app, bool enabled);
    static core::Status clear(const storage::ManagedRoot &root, const char *app);
    static core::Result<bool> begin(const storage::AppTxLock &lock,
                                    const storage::ManagedRoot &root, const char *app);
};

} // namespace lifecycle
} // namespace nbt

#endif
